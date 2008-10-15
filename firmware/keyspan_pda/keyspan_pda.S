/*  $Id: loop.s,v 1.23 2000/03/20 09:49:06 warner Exp $
 * 
 *  Firmware for the Keyspan PDA Serial Adapter, a USB serial port based on
 *  the EzUSB microcontroller.
 * 
 *  (C) Copyright 2000 Brian Warner <warner@lothar.com>
 * 
 * 	This program is free software; you can redistribute it and/or modify
 * 	it under the terms of the GNU General Public License as published by
 * 	the Free Software Foundation; either version 2 of the License, or
 * 	(at your option) any later version.
 * 
 *  "Keyspan PDA Serial Adapter" is probably a copyright of Keyspan, the
 *  company.
 * 
 *  This serial adapter is basically an EzUSB chip and an RS-232 line driver
 *  in a little widget that has a DB-9 on one end and a USB plug on the other.
 *  It uses the EzUSB's internal UART0 (using the pins from Port C) and timer2
 *  as a baud-rate generator. The wiring is:
 *   PC0/RxD0 <- rxd (DB9 pin 2)         PC4 <- dsr pin 6
 *   PC1/TxD0 -> txd pin 3               PC5 <- ri  pin 9
 *   PC2      -> rts pin 7               PC6 <- dcd pin 1
 *   PC3      <- cts pin 8               PC7 -> dtr pin 4
 *   PB1 -> line driver standby
 *
 *  The EzUSB register constants below come from their excellent documentation
 *  and sample code (which used to be available at www.anchorchips.com, but
 *  that has now been absorbed into Cypress' site and the CD-ROM contents
 *  don't appear to be available online anymore). If we get multiple
 *  EzUSB-based drivers into the kernel, it might be useful to pull them out
 *  into a separate .h file.
 * 
 * THEORY OF OPERATION:
 *
 *   There are two 256-byte ring buffers, one for tx, one for rx.
 *
 *   EP2out is pure tx data. When it appears, the data is copied into the tx
 *   ring and serial transmission is started if it wasn't already running. The
 *   "tx buffer empty" interrupt may kick off another character if the ring
 *   still has data. If the host is tx-blocked because the ring filled up,
 *   it will request a "tx unthrottle" interrupt. If sending a serial character
 *   empties the ring below the desired threshold, we set a bit that will send
 *   up the tx unthrottle message as soon as the rx buffer becomes free.
 *
 *   EP2in (interrupt) is used to send both rx chars and rx status messages
 *   (only "tx unthrottle" at this time) back up to the host. The first byte
 *   of the rx message indicates data (0) or status msg (1). Status messages
 *   are sent before any data.
 *
 *   Incoming serial characters are put into the rx ring by the serial
 *   interrupt, and the EP2in buffer sent if it wasn't already in transit.
 *   When the EP2in buffer returns, the interrupt prompts us to send more
 *   rx chars (or status messages) if they are pending.
 *
 *   Device control happens through "vendor specific" control messages on EP0.
 *   All messages are destined for the "Interface" (with the index always 0,
 *   so that if their two-port device might someday use similar firmware, we
 *   can use index=1 to refer to the second port). The messages defined are:
 *
 *    bRequest = 0 : set baud/bits/parity
 *               1 : unused
 *               2 : reserved for setting HW flow control (CTSRTS)
 *               3 : get/set "modem info" (pin states: DTR, RTS, DCD, RI, etc)
 *               4 : set break (on/off)
 *               5 : reserved for requesting interrupts on pin state change
 *               6 : query buffer room or chars in tx buffer
 *               7 : request tx unthrottle interrupt
 *
 *  The host-side driver is set to recognize the device ID values stashed in
 *  serial EEPROM (0x06cd, 0x0103), program this firmware into place, then
 *  start it running. This firmware will use EzUSB's "renumeration" trick by
 *  simulating a bus disconnect, then reconnect with a different device ID
 *  (encoded in the desc_device descriptor below). The host driver then
 *  recognizes the new device ID and glues it to the real serial driver code.
 *
 * USEFUL DOCS:
 *  EzUSB Technical Reference Manual: <http://www.anchorchips.com>
 *  8051 manuals: everywhere, but try www.dalsemi.com because the EzUSB is
 *   basically the Dallas enhanced 8051 code. Remember that the EzUSB IO ports
 *   use totally different registers!
 *  USB 1.1 spec: www.usb.org
 *
 * HOW TO BUILD:
 *  gcc -x assembler-with-cpp -P -E -o keyspan_pda.asm keyspan_pda.s
 *  as31 -l keyspan_pda.asm
 *  mv keyspan_pda.obj keyspan_pda.hex
 *  perl ezusb_convert.pl keyspan_pda < keyspan_pda.hex > keyspan_pda_fw.h
 * Get as31 from <http://www.pjrc.com/tech/8051/index.html>, and hack on it
 * a bit to make it build.
 *
 * THANKS:
 *  Greg Kroah-Hartman, for coordinating the whole usb-serial thing.
 *  AnchorChips, for making such an incredibly useful little microcontroller.
 *  KeySpan, for making a handy, cheap ($40) widget that was so easy to take
 *           apart and trace with an ohmmeter.
 *
 * TODO:
 *  lots. grep for TODO. Interrupt safety needs stress-testing. Better flow
 *  control. Interrupting host upon change in DCD, etc, counting transitions.
 *  Need to find a safe device id to use (the one used by the Keyspan firmware
 *  under Windows would be ideal.. can anyone figure out what it is?). Parity.
 *  More baud rates. Oh, and the string-descriptor-length silicon bug
 *  workaround should be implemented, but I'm lazy, and the consequence is
 *  that the device name strings that show up in your kernel log will have
 *  lots of trailing binary garbage in them (appears as ????). Device strings
 *  should be made more accurate.
 *
 * Questions, bugs, patches to Brian.
 *
 *  -Brian Warner <warner@lothar.com>
 *
 */
	
#define HIGH(x) (((x) & 0xff00) / 256)
#define LOW(x) ((x) & 0xff)

#define dpl1 0x84
#define dph1 0x85
#define dps 0x86

;;; our bit assignments
#define TX_RUNNING 0
#define DO_TX_UNTHROTTLE 1
	
	;; stack from 0x60 to 0x7f: should really set SP to 0x60-1, not 0x60
#define STACK #0x60-1

#define EXIF 0x91
#define EIE 0xe8
	.flag EUSB, EIE.0
	.flag ES0, IE.4

#define EP0CS #0x7fb4
#define EP0STALLbit #0x01
#define IN0BUF #0x7f00
#define IN0BC #0x7fb5
#define OUT0BUF #0x7ec0
#define OUT0BC #0x7fc5		
#define IN2BUF #0x7e00
#define IN2BC #0x7fb9
#define IN2CS #0x7fb8
#define OUT2BC #0x7fc9
#define OUT2CS #0x7fc8
#define OUT2BUF #0x7dc0
#define IN4BUF #0x7d00
#define IN4BC #0x7fbd
#define IN4CS #0x7fbc
#define OEB #0x7f9d
#define OUTB #0x7f97
#define OEC #0x7f9e
#define OUTC #0x7f98
#define PINSC #0x7f9b
#define PORTCCFG #0x7f95
#define IN07IRQ #0x7fa9
#define OUT07IRQ #0x7faa
#define IN07IEN #0x7fac
#define OUT07IEN #0x7fad
#define USBIRQ #0x7fab
#define USBIEN #0x7fae
#define USBBAV #0x7faf
#define USBCS #0x7fd6
#define SUDPTRH #0x7fd4
#define SUDPTRL #0x7fd5
#define SETUPDAT #0x7fe8
		
	;; usb interrupt : enable is EIE.0 (0xe8), flag is EXIF.4 (0x91)

	.org 0
	ljmp start
	;; interrupt vectors
	.org 23H
	ljmp serial_int
	.byte 0
	
	.org 43H
	ljmp USB_Jump_Table
	.byte 0			; filled in by the USB core

;;; local variables. These are not initialized properly: do it by hand.
	.org 30H
rx_ring_in:	.byte 0
rx_ring_out:	.byte 0
tx_ring_in:	.byte 0
tx_ring_out:	.byte 0
tx_unthrottle_threshold:	.byte 0
		
	.org 0x100H		; wants to be on a page boundary
USB_Jump_Table:
	ljmp	ISR_Sudav	; Setup Data Available
	.byte 0
	ljmp	0		; Start of Frame
	.byte 0
	ljmp	0		; Setup Data Loading
	.byte 0
	ljmp	0		; Global Suspend
	.byte 	0
	ljmp	0		; USB Reset  	
	.byte	0
	ljmp	0		; Reserved
	.byte	0
	ljmp	0		; End Point 0 In
	.byte	0
	ljmp	0		; End Point 0 Out
	.byte	0
	ljmp	0		; End Point 1 In
	.byte	0
	ljmp	0		; End Point 1 Out
	.byte	0
	ljmp	ISR_Ep2in
	.byte	0
	ljmp	ISR_Ep2out
	.byte	0


	.org 0x200
		
start:	mov SP,STACK-1 ; set stack
	;; clear local variables
	clr a
	mov tx_ring_in, a
	mov tx_ring_out, a
	mov rx_ring_in, a
	mov rx_ring_out, a
	mov tx_unthrottle_threshold, a
	clr TX_RUNNING
	clr DO_TX_UNTHROTTLE
	
	;; clear fifo with "fe"
	mov r1, 0
	mov a, #0xfe
	mov dptr, #tx_ring
clear_tx_ring_loop:
	movx @dptr, a
	inc dptr
	djnz r1, clear_tx_ring_loop

	mov a, #0xfd
	mov dptr, #rx_ring
clear_rx_ring_loop:
	movx @dptr, a
	inc dptr
	djnz r1, clear_rx_ring_loop

;;; turn on the RS-232 driver chip (bring the STANDBY pin low)
	;; set OEB.1
	mov a, #02H
	mov dptr,OEB
	movx @dptr,a
	;; clear PB1
	mov a, #00H
	mov dptr,OUTB
	movx @dptr,a
	;; set OEC.[127]
	mov a, #0x86
	mov dptr,OEC
	movx @dptr,a
	;; set PORTCCFG.[01] to route TxD0,RxD0 to serial port
	mov dptr, PORTCCFG
	mov a, #0x03
	movx @dptr, a
	
	;; set up interrupts, autovectoring
	mov dptr, USBBAV
	movx a,@dptr
	setb acc.0		; AVEN bit to 0
	movx @dptr, a

	mov a,#0x01		; enable SUDAV:	setup data available (for ep0)
	mov dptr, USBIRQ
	movx @dptr, a		; clear SUDAVI
	mov dptr, USBIEN
	movx @dptr, a
	
	mov dptr, IN07IEN
	mov a,#0x04		; enable IN2 int
	movx @dptr, a
	
	mov dptr, OUT07IEN
	mov a,#0x04		; enable OUT2 int
	movx @dptr, a
	mov dptr, OUT2BC
	movx @dptr, a		; arm OUT2

	mov a, #0x84		; turn on RTS, DTR
	mov dptr,OUTC
	movx @dptr, a
	;; setup the serial port. 9600 8N1.
	mov a,#01010011		; mode 1, enable rx, clear int
	mov SCON, a
	;;  using timer2, in 16-bit baud-rate-generator mode
	;;   (xtal 12MHz, internal fosc 24MHz)
	;;  RCAP2H,RCAP2L = 65536 - fosc/(32*baud)
	;;  57600: 0xFFF2.F, say 0xFFF3
	;;   9600: 0xFFB1.E, say 0xFFB2
	;;    300: 0xF63C
#define BAUD 9600
#define BAUD_TIMEOUT(rate) (65536 - (24 * 1000 * 1000) / (32 * rate))
#define BAUD_HIGH(rate) HIGH(BAUD_TIMEOUT(rate))
#define BAUD_LOW(rate) LOW(BAUD_TIMEOUT(rate))
		
	mov T2CON, #030h	; rclk=1,tclk=1,cp=0,tr2=0(enable later)
	mov r3, #5
	acall set_baud
	setb TR2
	mov SCON, #050h
	
#if 0
	mov r1, #0x40
	mov a, #0x41
send:	
	mov SBUF, a
	inc a
	anl a, #0x3F
	orl a, #0x40
;	xrl a, #0x02
wait1:	
	jnb TI, wait1
	clr TI
	djnz r1, send
;done:	sjmp done

#endif
	
	setb EUSB
	setb EA
	setb ES0
	;acall dump_stat

	;; hey, what say we RENUMERATE! (TRM p.62)
	mov a, #0
	mov dps, a
	mov dptr, USBCS
	mov a, #0x02		; DISCON=0, DISCOE=0, RENUM=1
	movx @dptr, a
	;; now presence pin is floating, simulating disconnect. wait 0.5s
	mov r1, #46
renum_wait1:
	mov r2, #0
renum_wait2:
	mov r3, #0
renum_wait3:
	djnz r3, renum_wait3
	djnz r2, renum_wait2
	djnz r1, renum_wait1	; wait about n*(256^2) 6MHz clocks
	mov a, #0x06		; DISCON=0, DISCOE=1, RENUM=1
	movx @dptr, a
	;; we are back online. the host device will now re-query us
	
	
main:	sjmp main

	

ISR_Sudav:
	push dps
	push dpl
	push dph
	push dpl1
	push dph1
	push acc
	mov a,EXIF
	clr acc.4
	mov EXIF,a		; clear INT2 first
	mov dptr, USBIRQ	; clear USB int
	mov a,#01h
	movx @dptr,a

	;; get request type
	mov dptr, SETUPDAT
	movx a, @dptr
	mov r1, a		; r1 = bmRequestType
	inc dptr
	movx a, @dptr
	mov r2, a		; r2 = bRequest
	inc dptr
	movx a, @dptr
	mov r3, a		; r3 = wValueL
	inc dptr
	movx a, @dptr
	mov r4, a		; r4 = wValueH

	;; main switch on bmRequest.type: standard or vendor
	mov a, r1
	anl a, #0x60
	cjne a, #0x00, setup_bmreq_type_not_standard
	;; standard request: now main switch is on bRequest
	ljmp setup_bmreq_is_standard
	
setup_bmreq_type_not_standard:	
	;; a still has bmreq&0x60
	cjne a, #0x40, setup_bmreq_type_not_vendor
	;; Anchor reserves bRequest 0xa0-0xaf, we use small ones
	;; switch on bRequest. bmRequest will always be 0x41 or 0xc1
	cjne r2, #0x00, setup_ctrl_not_00
	;; 00 is set baud, wValue[0] has baud rate index
	lcall set_baud		; index in r3, carry set if error
	jc setup_bmreq_type_not_standard__do_stall
	ljmp setup_done_ack
setup_bmreq_type_not_standard__do_stall:
	ljmp setup_stall
setup_ctrl_not_00:
	cjne r2, #0x01, setup_ctrl_not_01
	;; 01 is reserved for set bits (parity). TODO
	ljmp setup_stall
setup_ctrl_not_01:
	cjne r2, #0x02, setup_ctrl_not_02
	;; 02 is set HW flow control. TODO
	ljmp setup_stall
setup_ctrl_not_02:
	cjne r2, #0x03, setup_ctrl_not_03
	;; 03 is control pins (RTS, DTR).
	ljmp control_pins	; will jump to setup_done_ack,
				;  or setup_return_one_byte
setup_ctrl_not_03:
	cjne r2, #0x04, setup_ctrl_not_04
	;; 04 is send break (really "turn break on/off"). TODO
	cjne r3, #0x00, setup_ctrl_do_break_on
	;; do break off: restore PORTCCFG.1 to reconnect TxD0 to serial port
	mov dptr, PORTCCFG
	movx a, @dptr
	orl a, #0x02
	movx @dptr, a
	ljmp setup_done_ack
setup_ctrl_do_break_on:
	;; do break on: clear PORTCCFG.0, set TxD high(?) (b1 low)
	mov dptr, OUTC
	movx a, @dptr
	anl a, #0xfd		; ~0x02
	movx @dptr, a
	mov dptr, PORTCCFG
	movx a, @dptr
	anl a, #0xfd		; ~0x02
	movx @dptr, a
	ljmp setup_done_ack
setup_ctrl_not_04:
	cjne r2, #0x05, setup_ctrl_not_05
	;; 05 is set desired interrupt bitmap. TODO
	ljmp setup_stall
setup_ctrl_not_05:
	cjne r2, #0x06, setup_ctrl_not_06
	;; 06 is query room
	cjne r3, #0x00, setup_ctrl_06_not_00
	;; 06, wValue[0]=0 is query write_room
	mov a, tx_ring_out
	setb c
	subb a, tx_ring_in	; out-1-in = 255 - (in-out)
	ljmp setup_return_one_byte
setup_ctrl_06_not_00:
	cjne r3, #0x01, setup_ctrl_06_not_01
	;; 06, wValue[0]=1 is query chars_in_buffer
	mov a, tx_ring_in
	clr c
	subb a, tx_ring_out	; in-out
	ljmp setup_return_one_byte
setup_ctrl_06_not_01:	
	ljmp setup_stall
setup_ctrl_not_06:
	cjne r2, #0x07, setup_ctrl_not_07
	;; 07 is request tx unthrottle interrupt
	mov tx_unthrottle_threshold, r3; wValue[0] is threshold value
	ljmp setup_done_ack
setup_ctrl_not_07:
	ljmp setup_stall
	
setup_bmreq_type_not_vendor:
	ljmp setup_stall


setup_bmreq_is_standard:	
	cjne r2, #0x00, setup_breq_not_00
	;; 00:	Get_Status (sub-switch on bmRequestType: device, ep, int)
	cjne r1, #0x80, setup_Get_Status_not_device
	;; Get_Status(device)
	;;  are we self-powered? no. can we do remote wakeup? no
	;;   so return two zero bytes. This is reusable
setup_return_two_zero_bytes:
	mov dptr, IN0BUF
	clr a
	movx @dptr, a
	inc dptr
	movx @dptr, a
	mov dptr, IN0BC
	mov a, #2
	movx @dptr, a
	ljmp setup_done_ack
setup_Get_Status_not_device:
	cjne r1, #0x82, setup_Get_Status_not_endpoint
	;; Get_Status(endpoint)
	;;  must get stall bit for ep[wIndexL], return two bytes, bit in lsb 0
	;; for now: cheat. TODO
	sjmp setup_return_two_zero_bytes
setup_Get_Status_not_endpoint:
	cjne r1, #0x81, setup_Get_Status_not_interface
	;; Get_Status(interface): return two zeros
	sjmp setup_return_two_zero_bytes
setup_Get_Status_not_interface:	
	ljmp setup_stall
	
setup_breq_not_00:
	cjne r2, #0x01, setup_breq_not_01
	;; 01:	Clear_Feature (sub-switch on wValueL: stall, remote wakeup)
	cjne r3, #0x00, setup_Clear_Feature_not_stall
	;; Clear_Feature(stall). should clear a stall bit. TODO
	ljmp setup_stall
setup_Clear_Feature_not_stall:
	cjne r3, #0x01, setup_Clear_Feature_not_rwake
	;; Clear_Feature(remote wakeup). ignored.
	ljmp setup_done_ack
setup_Clear_Feature_not_rwake:
	ljmp setup_stall
	
setup_breq_not_01:
	cjne r2, #0x03, setup_breq_not_03
	;; 03:	Set_Feature (sub-switch on wValueL: stall, remote wakeup)
	cjne r3, #0x00, setup_Set_Feature_not_stall
	;; Set_Feature(stall). Should set a stall bit. TODO
	ljmp setup_stall
setup_Set_Feature_not_stall:
	cjne r3, #0x01, setup_Set_Feature_not_rwake
	;; Set_Feature(remote wakeup). ignored.
	ljmp setup_done_ack
setup_Set_Feature_not_rwake:
	ljmp setup_stall
	
setup_breq_not_03:	
	cjne r2, #0x06, setup_breq_not_06
	;; 06:	Get_Descriptor (s-switch on wValueH: dev, config[n], string[n])
	cjne r4, #0x01, setup_Get_Descriptor_not_device
	;; Get_Descriptor(device)
	mov dptr, SUDPTRH
	mov a, #HIGH(desc_device)
	movx @dptr, a
	mov dptr, SUDPTRL
	mov a, #LOW(desc_device)
	movx @dptr, a
	ljmp setup_done_ack
setup_Get_Descriptor_not_device:
	cjne r4, #0x02, setup_Get_Descriptor_not_config
	;; Get_Descriptor(config[n])
	cjne r3, #0x00, setup_stall; only handle n==0
	;; Get_Descriptor(config[0])
	mov dptr, SUDPTRH
	mov a, #HIGH(desc_config1)
	movx @dptr, a
	mov dptr, SUDPTRL
	mov a, #LOW(desc_config1)
	movx @dptr, a
	ljmp setup_done_ack
setup_Get_Descriptor_not_config:
	cjne r4, #0x03, setup_Get_Descriptor_not_string
	;; Get_Descriptor(string[wValueL])
	;;  if (wValueL >= maxstrings) stall
	mov a, #((desc_strings_end-desc_strings)/2)
	clr c
	subb a,r3		; a=4, r3 = 0..3 . if a<=0 then stall
	jc  setup_stall
	jz  setup_stall
	mov a, r3
	add a, r3		; a = 2*wValueL
	mov dptr, #desc_strings
	add a, dpl
	mov dpl, a
	mov a, #0
	addc a, dph
	mov dph, a		; dph = desc_strings[a]. big endian! (handy)
	;; it looks like my adapter uses a revision of the EZUSB that
	;; contains "rev D errata number 8", as hinted in the EzUSB example
	;; code. I cannot find an actual errata description on the Cypress
	;; web site, but from the example code it looks like this bug causes
	;; the length of string descriptors to be read incorrectly, possibly
	;; sending back more characters than the descriptor has. The workaround
	;; is to manually send out all of the data. The consequence of not
	;; using the workaround is that the strings gathered by the kernel
	;; driver are too long and are filled with trailing garbage (including
	;; leftover strings). Writing this out by hand is a nuisance, so for
	;; now I will just live with the bug.
	movx a, @dptr
	mov r1, a
	inc dptr
	movx a, @dptr
	mov r2, a
	mov dptr, SUDPTRH
	mov a, r1
	movx @dptr, a
	mov dptr, SUDPTRL
	mov a, r2
	movx @dptr, a
	;; done
	ljmp setup_done_ack
	
setup_Get_Descriptor_not_string:
	ljmp setup_stall
	
setup_breq_not_06:
	cjne r2, #0x08, setup_breq_not_08
	;; Get_Configuration. always 1. return one byte.
	;; this is reusable
	mov a, #1
setup_return_one_byte:	
	mov dptr, IN0BUF
	movx @dptr, a
	mov a, #1
	mov dptr, IN0BC
	movx @dptr, a
	ljmp setup_done_ack
setup_breq_not_08:
	cjne r2, #0x09, setup_breq_not_09
	;; 09: Set_Configuration. ignored.
	ljmp setup_done_ack
setup_breq_not_09:
	cjne r2, #0x0a, setup_breq_not_0a
	;; 0a: Get_Interface. get the current altsetting for int[wIndexL]
	;;  since we only have one interface, ignore wIndexL, return a 0
	mov a, #0
	ljmp setup_return_one_byte
setup_breq_not_0a:
	cjne r2, #0x0b, setup_breq_not_0b
	;; 0b: Set_Interface. set altsetting for interface[wIndexL]. ignored
	ljmp setup_done_ack
setup_breq_not_0b:
	ljmp setup_stall

		
setup_done_ack:	
	;; now clear HSNAK
	mov dptr, EP0CS
	mov a, #0x02
	movx @dptr, a
	sjmp setup_done
setup_stall:	
	;; unhandled. STALL
	;EP0CS |= bmEPSTALL
	mov dptr, EP0CS
	movx a, @dptr
	orl a, EP0STALLbit
	movx @dptr, a
	sjmp setup_done
	
setup_done:	
	pop acc
	pop dph1
	pop dpl1
	pop dph
	pop dpl
	pop dps
	reti

;;; ==============================================================
	
set_baud:			; baud index in r3
	;; verify a < 10
	mov a, r3
	jb ACC.7, set_baud__badbaud
	clr c
	subb a, #10
	jnc set_baud__badbaud
	mov a, r3
	rl a			; a = index*2
	add a, #LOW(baud_table)
	mov dpl, a
	mov a, #HIGH(baud_table)
	addc a, #0
	mov dph, a
	;; TODO: shut down xmit/receive
	;; TODO: wait for current xmit char to leave
	;; TODO: shut down timer to avoid partial-char glitch
	movx a,@dptr		; BAUD_HIGH
	mov RCAP2H, a
	mov TH2, a
	inc dptr
	movx a,@dptr		; BAUD_LOW
	mov RCAP2L, a
	mov TL2, a
	;; TODO: restart xmit/receive
	;; TODO: reenable interrupts, resume tx if pending
	clr c			; c=0: success
	ret
set_baud__badbaud:
	setb c			; c=1: failure
	ret
	
;;; ==================================================
control_pins:
	cjne r1, #0x41, control_pins_in
control_pins_out:
	mov a, r3 ; wValue[0] holds new bits:	b7 is new DTR, b2 is new RTS
	xrl a, #0xff		; 1 means active, 0V, +12V ?
	anl a, #0x84
	mov r3, a
	mov dptr, OUTC
	movx a, @dptr		; only change bits 7 and 2
	anl a, #0x7b		; ~0x84
	orl a, r3
	movx @dptr, a		; other pins are inputs, bits ignored
	ljmp setup_done_ack
control_pins_in:
	mov dptr, PINSC
	movx a, @dptr
	xrl a, #0xff
	ljmp setup_return_one_byte

;;; ========================================
	
ISR_Ep2in:
	push dps
	push dpl
	push dph
	push dpl1
	push dph1
	push acc
	mov a,EXIF
	clr acc.4
	mov EXIF,a		; clear INT2 first
	mov dptr, IN07IRQ	; clear USB int
	mov a,#04h
	movx @dptr,a

	;; do stuff
	lcall start_in
	
	pop acc
	pop dph1
	pop dpl1
	pop dph
	pop dpl
	pop dps
	reti

ISR_Ep2out:
	push dps
	push dpl
	push dph
	push dpl1
	push dph1
	push acc
	mov a,EXIF
	clr acc.4
	mov EXIF,a		; clear INT2 first
	mov dptr, OUT07IRQ	; clear USB int
	mov a,#04h
	movx @dptr,a

	;; do stuff

	;; copy data into buffer. for now, assume we will have enough space
	mov dptr, OUT2BC	; get byte count
	movx a,@dptr
	mov r1, a
	clr a
	mov dps, a
	mov dptr, OUT2BUF	; load DPTR0 with source
	mov dph1, #HIGH(tx_ring)	; load DPTR1 with target
	mov dpl1, tx_ring_in
OUT_loop:
	movx a,@dptr		; read
	inc dps			; switch to DPTR1: target
	inc dpl1		; target = tx_ring_in+1
	movx @dptr,a		; store
	mov a,dpl1
	cjne a, tx_ring_out, OUT_no_overflow
	sjmp OUT_overflow
OUT_no_overflow:	
	inc tx_ring_in		; tx_ring_in++
	inc dps			; switch to DPTR0: source
	inc dptr
	djnz r1, OUT_loop
	sjmp OUT_done
OUT_overflow:
	;; signal overflow
	;; fall through
OUT_done:	
	;; ack
	mov dptr,OUT2BC
	movx @dptr,a

	;; start tx
	acall maybe_start_tx
	;acall dump_stat
	
	pop acc
	pop dph1
	pop dpl1
	pop dph
	pop dpl
	pop dps
	reti

dump_stat:
	;; fill in EP4in with a debugging message:
	;;   tx_ring_in, tx_ring_out, rx_ring_in, rx_ring_out
	;;   tx_active
	;;   tx_ring[0..15]
	;;   0xfc
	;;   rx_ring[0..15]
	clr a
	mov dps, a
	
	mov dptr, IN4CS
	movx a, @dptr
	jb acc.1, dump_stat__done; busy: cannot dump, old one still pending
	mov dptr, IN4BUF
	
	mov a, tx_ring_in
	movx @dptr, a
	inc dptr
	mov a, tx_ring_out
	movx @dptr, a
	inc dptr

	mov a, rx_ring_in
	movx @dptr, a
	inc dptr
	mov a, rx_ring_out
	movx @dptr, a
	inc dptr
	
	clr a
	jnb TX_RUNNING, dump_stat__no_tx_running
	inc a
dump_stat__no_tx_running:
	movx @dptr, a
	inc dptr
	;; tx_ring[0..15]
	inc dps
	mov dptr, #tx_ring	; DPTR1: source
	mov r1, #16
dump_stat__tx_ring_loop:
	movx a, @dptr
	inc dptr
	inc dps
	movx @dptr, a
	inc dptr
	inc dps
	djnz r1, dump_stat__tx_ring_loop
	inc dps
	
	mov a, #0xfc
	movx @dptr, a
	inc dptr
	
	;; rx_ring[0..15]
	inc dps
	mov dptr, #rx_ring	; DPTR1: source
	mov r1, #16
dump_stat__rx_ring_loop:
	movx a, @dptr
	inc dptr
	inc dps
	movx @dptr, a
	inc dptr
	inc dps
	djnz r1, dump_stat__rx_ring_loop
	
	;; now send it
	clr a
	mov dps, a
	mov dptr, IN4BC
	mov a, #38
	movx @dptr, a
dump_stat__done:	
	ret
		
;;; ============================================================
	
maybe_start_tx:
	;; make sure the tx process is running.
	jb TX_RUNNING, start_tx_done
start_tx:
	;; is there work to be done?
	mov a, tx_ring_in
	cjne a,tx_ring_out, start_tx__work
	ret			; no work
start_tx__work:	
	;; tx was not running. send the first character, setup the TI int
	inc tx_ring_out		; [++tx_ring_out]
	mov dph, #HIGH(tx_ring)
	mov dpl, tx_ring_out
	movx a, @dptr
	mov sbuf, a
	setb TX_RUNNING
start_tx_done:
	;; can we unthrottle the host tx process?
	;;  step 1: do we care?
	mov a, #0
	cjne a, tx_unthrottle_threshold, start_tx__maybe_unthrottle_tx
	;; nope
start_tx_really_done:
	ret
start_tx__maybe_unthrottle_tx:
	;;  step 2: is there now room?
	mov a, tx_ring_out
	setb c
	subb a, tx_ring_in
	;; a is now write_room. If thresh >= a, we can unthrottle
	clr c
	subb a, tx_unthrottle_threshold
	jc start_tx_really_done	; nope
	;; yes, we can unthrottle. remove the threshold and mark a request
	mov tx_unthrottle_threshold, #0
	setb DO_TX_UNTHROTTLE
	;; prod rx, which will actually send the message when in2 becomes free
	ljmp start_in
	

serial_int:
	push dps
	push dpl
	push dph
	push dpl1
	push dph1
	push acc
	jnb TI, serial_int__not_tx
	;; tx finished. send another character if we have one
	clr TI			; clear int
	clr TX_RUNNING
	lcall start_tx
serial_int__not_tx:
	jnb RI, serial_int__not_rx
	lcall get_rx_char
	clr RI			; clear int
serial_int__not_rx:	
	;; return
	pop acc
	pop dph1
	pop dpl1
	pop dph
	pop dpl
	pop dps
	reti

get_rx_char:
	mov dph, #HIGH(rx_ring)
	mov dpl, rx_ring_in
	inc dpl			; target = rx_ring_in+1
	mov a, sbuf
	movx @dptr, a
	;; check for overflow before incrementing rx_ring_in
	mov a, dpl
	cjne a, rx_ring_out, get_rx_char__no_overflow
	;; signal overflow
	ret
get_rx_char__no_overflow:	
	inc rx_ring_in
	;; kick off USB INpipe
	acall start_in
	ret

start_in:
	;; check if the inpipe is already running.
	mov dptr, IN2CS
	movx a, @dptr
	jb acc.1, start_in__done; int will handle it
	jb DO_TX_UNTHROTTLE, start_in__do_tx_unthrottle
	;; see if there is any work to do. a serial interrupt might occur
	;; during this sequence?
	mov a, rx_ring_in
	cjne a, rx_ring_out, start_in__have_work
	ret			; nope
start_in__have_work:	
	;; now copy as much data as possible into the pipe. 63 bytes max.
	clr a
	mov dps, a
	mov dph, #HIGH(rx_ring)	; load DPTR0 with source
	inc dps
	mov dptr, IN2BUF	; load DPTR1 with target
	movx @dptr, a		; in[0] signals that rest of IN is rx data
	inc dptr
	inc dps
	;; loop until we run out of data, or we have copied 64 bytes
	mov r1, #1		; INbuf size counter
start_in__loop:
	mov a, rx_ring_in
	cjne a, rx_ring_out, start_inlocal_irq_enablell_copying
	sjmp start_in__kick
start_inlocal_irq_enablell_copying:
	inc rx_ring_out
	mov dpl, rx_ring_out
	movx a, @dptr
	inc dps
	movx @dptr, a		; write into IN buffer
	inc dptr
	inc dps
	inc r1
	cjne r1, #64, start_in__loop; loop
start_in__kick:
	;; either we ran out of data, or we copied 64 bytes. r1 has byte count
	;; kick off IN
	mov dptr, IN2BC
	mov a, r1
	jz start_in__done
	movx @dptr, a
	;; done
start_in__done:
	;acall dump_stat
	ret
start_in__do_tx_unthrottle:
	;; special sequence: send a tx unthrottle message
	clr DO_TX_UNTHROTTLE
	clr a
	mov dps, a
	mov dptr, IN2BUF
	mov a, #1
	movx @dptr, a
	inc dptr
	mov a, #2
	movx @dptr, a
	mov dptr, IN2BC
	movx @dptr, a
	ret
	
putchar:
	clr TI
	mov SBUF, a
putchar_wait:
	jnb TI, putchar_wait
	clr TI
	ret

	
baud_table:			; baud_high, then baud_low
	;; baud[0]: 110
	.byte BAUD_HIGH(110)
	.byte BAUD_LOW(110)
	;; baud[1]: 300
	.byte BAUD_HIGH(300)
	.byte BAUD_LOW(300)
	;; baud[2]: 1200
	.byte BAUD_HIGH(1200)
	.byte BAUD_LOW(1200)
	;; baud[3]: 2400
	.byte BAUD_HIGH(2400)
	.byte BAUD_LOW(2400)
	;; baud[4]: 4800
	.byte BAUD_HIGH(4800)
	.byte BAUD_LOW(4800)
	;; baud[5]: 9600
	.byte BAUD_HIGH(9600)
	.byte BAUD_LOW(9600)
	;; baud[6]: 19200
	.byte BAUD_HIGH(19200)
	.byte BAUD_LOW(19200)
	;; baud[7]: 38400
	.byte BAUD_HIGH(38400)
	.byte BAUD_LOW(38400)
	;; baud[8]: 57600
	.byte BAUD_HIGH(57600)
	.byte BAUD_LOW(57600)
	;; baud[9]: 115200
	.byte BAUD_HIGH(115200)
	.byte BAUD_LOW(115200)

desc_device:
	.byte 0x12, 0x01, 0x00, 0x01, 0xff, 0xff, 0xff, 0x40
	.byte 0xcd, 0x06, 0x04, 0x01, 0x89, 0xab, 1, 2, 3, 0x01
;;; The "real" device id, which must match the host driver, is that
;;; "0xcd 0x06 0x04 0x01" sequence, which is 0x06cd, 0x0104
	
desc_config1:
	.byte 0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32
	.byte 0x09, 0x04, 0x00, 0x00, 0x02, 0xff, 0xff, 0xff, 0x00
	.byte 0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x01
	.byte 0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00

desc_strings:
	.word string_langids, string_mfg, string_product, string_serial
desc_strings_end:

string_langids:	.byte string_langids_end-string_langids
	.byte 3
	.word 0
string_langids_end:

	;; sigh. These strings are Unicode, meaning UTF16? 2 bytes each. Now
	;; *that* is a pain in the ass to encode. And they are little-endian
	;; too. Use this perl snippet to get the bytecodes:
	/* while (<>) {
	    @c = split(//);
	    foreach $c (@c) {
	     printf("0x%02x, 0x00, ", ord($c));
	    }
	   }
	*/

string_mfg:	.byte string_mfg_end-string_mfg
	.byte 3
;	.byte "ACME usb widgets"
	.byte 0x41, 0x00, 0x43, 0x00, 0x4d, 0x00, 0x45, 0x00, 0x20, 0x00, 0x75, 0x00, 0x73, 0x00, 0x62, 0x00, 0x20, 0x00, 0x77, 0x00, 0x69, 0x00, 0x64, 0x00, 0x67, 0x00, 0x65, 0x00, 0x74, 0x00, 0x73, 0x00
string_mfg_end:
	
string_product:	.byte string_product_end-string_product
	.byte 3
;	.byte "ACME USB serial widget"
	.byte 0x41, 0x00, 0x43, 0x00, 0x4d, 0x00, 0x45, 0x00, 0x20, 0x00, 0x55, 0x00, 0x53, 0x00, 0x42, 0x00, 0x20, 0x00, 0x73, 0x00, 0x65, 0x00, 0x72, 0x00, 0x69, 0x00, 0x61, 0x00, 0x6c, 0x00, 0x20, 0x00, 0x77, 0x00, 0x69, 0x00, 0x64, 0x00, 0x67, 0x00, 0x65, 0x00, 0x74, 0x00
string_product_end:
	
string_serial:	.byte string_serial_end-string_serial
	.byte 3
;	.byte "47"
	.byte 0x34, 0x00, 0x37, 0x00
string_serial_end:
		
;;; ring buffer memory
	;; tx_ring_in+1 is where the next input byte will go
	;; [tx_ring_out] has been sent
	;; if tx_ring_in == tx_ring_out, theres no work to do
	;; there are (tx_ring_in - tx_ring_out) chars to be written
	;; dont let _in lap _out
	;;   cannot inc if tx_ring_in+1 == tx_ring_out
	;;  write [tx_ring_in+1] then tx_ring_in++
	;;   if (tx_ring_in+1 == tx_ring_out), overflow
	;;   else tx_ring_in++
	;;  read/send [tx_ring_out+1], then tx_ring_out++

	;; rx_ring_in works the same way
	
	.org 0x1000
tx_ring:
	.skip 0x100		; 256 bytes
rx_ring:
	.skip 0x100		; 256 bytes
	
	
	.END
	
