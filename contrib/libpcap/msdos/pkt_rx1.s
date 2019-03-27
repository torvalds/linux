;
; This file requires NASM 0.97+ to assemble
;
; Currently used only for djgpp + DOS4GW targets
;
; these sizes MUST be equal to the sizes in PKTDRVR.H
;
%define  ETH_MTU     1500                  ; max data size on Ethernet
%define  ETH_MIN     60                    ; min/max total frame size
%define  ETH_MAX     (ETH_MTU+2*6+2)       ; =1514
%define  NUM_RX_BUF  32                    ; # of RX element buffers
%define  RX_SIZE     (ETH_MAX+6)           ; sizeof(RX_ELEMENT) = 1514+6
%idefine offset

struc RX_ELEMENT
      .firstCount  resw 1                  ; # of bytes on 1st call
      .secondCount resw 1                  ; # of bytes on 2nd call
      .handle      resw 1                  ; handle for upcall
    ; .timeStamp   resw 4                  ; 64-bit RDTSC value
      .destinAdr   resb 6                  ; packet destination address
      .sourceAdr   resb 6                  ; packet source address
      .protocol    resw 1                  ; packet protocol number
      .rxBuffer    resb ETH_MTU            ; RX buffer
endstruc

;-------------------------------------------

[org 0]  ; assemble to .bin file

_rxOutOfs   dw   offset _pktRxBuf          ; ring buffer offsets
_rxInOfs    dw   offset _pktRxBuf          ; into _pktRxBuf
_pktDrop    dw   0,0                       ; packet drop counter
_pktTemp    resb 20                        ; temp work area
_pktTxBuf   resb (ETH_MAX)                 ; TX buffer
_pktRxBuf   resb (RX_SIZE*NUM_RX_BUF)      ; RX structures
 LAST_OFS   equ  $

screenSeg   dw  0B800h
newInOffset dw  0

fanChars    db  '-\|/'
fanIndex    dw  0

%macro SHOW_RX 0
       push es
       push bx
       mov bx, [screenSeg]
       mov es, bx                    ;; r-mode segment of colour screen
       mov di, 158                   ;; upper right corner - 1
       mov bx, [fanIndex]
       mov al, [fanChars+bx]         ;; get write char
       mov ah, 15                    ;;  and white colour
       cld                           ;; Needed?
       stosw                         ;; write to screen at ES:EDI
       inc word [fanIndex]           ;; update next index
       and word [fanIndex], 3
       pop bx
       pop es
%endmacro

;PutTimeStamp
;       rdtsc
;       mov [si].timeStamp, eax
;       mov [si+4].timeStamp, edx
;       ret


;------------------------------------------------------------------------
;
; This routine gets called by the packet driver twice:
;   1st time (AX=0) it requests an address where to put the packet
;
;   2nd time (AX=1) the packet has been copied to this location (DS:SI)
;   BX has client handle (stored in RX_ELEMENT.handle).
;   CX has # of bytes in packet on both call. They should be equal.
; A test for equality is done by putting CX in _pktRxBuf [n].firstCount
; and _pktRxBuf[n].secondCount, and CL on first call in
; _pktRxBuf[n].rxBuffer[CX]. These values are checked in "PktReceive"
; (PKTDRVR.C)
;
;---------------------------------------------------------------------

_PktReceiver:
         pushf
         cli                         ; no distraction wanted !
         push ds
         push bx
         mov bx, cs
         mov ds, bx
         mov es, bx                  ; ES = DS = CS or seg _DATA
         pop bx                      ; restore handle

         cmp ax, 0                   ; first call? (AX=0)
         jne @post                   ; AX=1: second call, do post process

%ifdef DEBUG
         SHOW_RX                     ; show that a packet is received
%endif

         cmp cx, ETH_MAX             ; size OK ?
         ja  @skip                   ; no, too big

         mov ax, [_rxInOfs]
         add ax, RX_SIZE
         cmp ax, LAST_OFS
         jb  @noWrap
         mov ax, offset _pktRxBuf
@noWrap:
         cmp ax, [_rxOutOfs]
         je  @dump
         mov di, [_rxInOfs]          ; ES:DI -> _pktRxBuf[n]
         mov [newInOffset], ax

         mov [di], cx                ; remember firstCount.
         mov [di+4], bx              ; remember handle.
         add di, 6                   ; ES:DI -> _pktRxBuf[n].destinAdr
         pop ds
         popf
         retf                        ; far return to driver with ES:DI

@dump:   add word [_pktDrop+0], 1    ; discard the packet on 1st call
         adc word [_pktDrop+2], 0    ; increment packets lost

@skip:   xor di, di                  ; return ES:DI = NIL pointer
         xor ax, ax
         mov es, ax
         pop ds
         popf
         retf

@post:   or si, si                   ; DS:SI->_pktRxBuf[n][n].destinAdr
         jz @discard                 ; make sure we don't use NULL-pointer

       ;
       ; push si
       ; call bpf_filter_match       ; run the filter here some day
       ; pop si
       ; cmp ax, 0
       ; je  @discard

         mov [si-6+2], cx            ; store _pktRxBuf[n].secondCount
         mov ax, [newInOffset]
         mov [_rxInOfs], ax          ; update _pktRxBuf input offset

       ; call PutTimeStamp

@discard:
         pop ds
         popf
         retf

_pktRxEnd  db 0                      ; marker for end of r-mode code/data

END

