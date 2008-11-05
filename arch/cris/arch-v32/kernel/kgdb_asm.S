/*
 *  Copyright (C) 2004 Axis Communications AB
 *
 * Code for handling break 8, hardware breakpoint, single step, and serial
 * port exceptions for kernel debugging purposes.
 */

#include <arch/hwregs/intr_vect.h>

	;; Exported functions.
	.globl kgdb_handle_exception

kgdb_handle_exception:

;; Create a register image of the caller.
;;
;; First of all, save the ACR on the stack since we need it for address calculations.
;; We put it into the register struct later.

  subq     4, $sp
  move.d   $acr, [$sp]

;; Now we are free to use ACR all we want.
;; If we were running this handler with interrupts on, we would have to be careful
;; to save and restore CCS manually, but since we aren't we treat it like every other
;; register.

  move.d   reg,  $acr
  move.d   $r0,  [$acr]        ; Save R0 (start of register struct)
  addq     4,    $acr
  move.d   $r1,  [$acr]        ; Save R1
  addq     4,    $acr
  move.d   $r2,  [$acr]        ; Save R2
  addq     4,    $acr
  move.d   $r3,  [$acr]        ; Save R3
  addq     4,    $acr
  move.d   $r4,  [$acr]        ; Save R4
  addq     4,    $acr
  move.d   $r5,  [$acr]        ; Save R5
  addq     4,    $acr
  move.d   $r6,  [$acr]        ; Save R6
  addq     4,    $acr
  move.d   $r7,  [$acr]        ; Save R7
  addq     4,    $acr
  move.d   $r8,  [$acr]        ; Save R8
  addq     4,    $acr
  move.d   $r9,  [$acr]        ; Save R9
  addq     4,    $acr
  move.d   $r10, [$acr]        ; Save R10
  addq     4,    $acr
  move.d   $r11, [$acr]        ; Save R11
  addq     4,    $acr
  move.d   $r12, [$acr]        ; Save R12
  addq     4,    $acr
  move.d   $r13, [$acr]        ; Save R13
  addq     4,    $acr
  move.d   $sp,  [$acr]        ; Save SP (R14)
  addq     4,    $acr

  ;; The ACR register is already saved on the stack, so pop it from there.
  move.d   [$sp],$r0
  move.d   $r0,  [$acr]
  addq     4,    $acr

  move     $bz,  [$acr]
  addq     1,    $acr
  move     $vr,  [$acr]
  addq     1,    $acr
  move     $pid, [$acr]
  addq     4,    $acr
  move     $srs, [$acr]
  addq     1,    $acr
  move     $wz,  [$acr]
  addq     2,    $acr
  move     $exs, [$acr]
  addq     4,    $acr
  move     $eda, [$acr]
  addq     4,    $acr
  move     $mof, [$acr]
  addq     4,    $acr
  move     $dz,  [$acr]
  addq     4,    $acr
  move     $ebp, [$acr]
  addq     4,    $acr
  move     $erp, [$acr]
  addq     4,    $acr
  move     $srp, [$acr]
  addq     4,    $acr
  move     $nrp, [$acr]
  addq     4,    $acr
  move     $ccs, [$acr]
  addq     4,    $acr
  move     $usp, [$acr]
  addq     4,    $acr
  move     $spc, [$acr]
  addq     4,     $acr

;; Skip the pseudo-PC.
  addq     4,     $acr

;; Save the support registers in bank 0 - 3.
  clear.d $r1 ; Bank counter
  move.d  sreg, $acr

;; Bank 0
  move    $r1,  $srs
  nop
  nop
  nop
  move    $s0,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s1,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s2,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s3,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s4,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s5,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s6,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s7,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s8,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s9,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s10,  $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s11,  $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s12,  $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr

  ;; Nothing in S13 - S15, bank 0
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr

;; Bank 1 and bank 2 have the same layout, hence the loop.
  addq    1, $r1
1:
  move    $r1,  $srs
  nop
  nop
  nop
  move    $s0,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s1,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s2,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s3,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s4,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s5,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s6,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr

  ;; Nothing in S7 - S15, bank 1 and 2
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr
  clear.d [$acr]
  addq    4,     $acr

  addq 1, $r1
  cmpq 3, $r1
  bne 1b
  nop

;; Bank 3
  move    $r1,  $srs
  nop
  nop
  nop
  move    $s0,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s1,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s2,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s3,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s4,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s5,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s6,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s7,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s8,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s9,   $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s10,  $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s11,  $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s12,  $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s13,  $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
  move    $s14,  $r0
  move.d  $r0,   [$acr]
  addq    4,     $acr
;; Nothing in S15, bank 3
  clear.d [$acr]
  addq    4,     $acr

;; Check what got us here: get IDX field of EXS.
  move $exs,    $r10
  and.d 0xff00, $r10
  lsrq 8,       $r10
#if defined(CONFIG_ETRAX_KGDB_PORT0)
  cmp.d SER0_INTR_VECT,   $r10 ; IRQ for serial port 0
  beq sigint
  nop
#elif defined(CONFIG_ETRAX_KGDB_PORT1)
  cmp.d SER1_INTR_VECT,   $r10 ; IRQ for serial port 1
  beq sigint
  nop
#elif defined(CONFIG_ETRAX_KGDB_PORT2)
  cmp.d SER2_INTR_VECT,   $r10 ; IRQ for serial port 2
  beq sigint
  nop
#elif defined(CONFIG_ETRAX_KGDB_PORT3)
  cmp.d SER3_INTR_VECT,   $r10 ; IRQ for serial port 3
  beq sigint
  nop
#endif
;; Multiple interrupt must be due to serial break.
  cmp.d 0x30,   $r10 ; Multiple interrupt
  beq sigint
  nop
;; Neither of those? Then it's a sigtrap.
  ba handle_comm
  moveq 5, $r10      ; Set SIGTRAP (delay slot)

sigint:
  ;; Serial interrupt; get character
  jsr getDebugChar
  nop                ; Delay slot
  cmp.b 3, $r10      ; \003 (Ctrl-C)?
  bne return         ; No, get out of here
  nop
  moveq 2, $r10      ; Set SIGINT

;;
;; Handle the communication
;;
handle_comm:
  move.d   internal_stack+1020, $sp ; Use the internal stack which grows upwards
  jsr      handle_exception         ; Interactive routine
  nop

;;
;; Return to the caller
;;
return:

;; First of all, write the support registers.
  clear.d $r1 ; Bank counter
  move.d  sreg, $acr

;; Bank 0
  move    $r1,  $srs
  nop
  nop
  nop
  move.d  [$acr], $r0
  move    $r0,    $s0
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s1
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s2
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s3
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s4
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s5
  addq    4,      $acr

;; Nothing in S6 - S7, bank 0.
  addq    4,      $acr
  addq    4,      $acr

  move.d  [$acr], $r0
  move    $r0,    $s8
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s9
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s10
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s11
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s12
  addq    4,      $acr

;; Nothing in S13 - S15, bank 0
  addq    4,      $acr
  addq    4,      $acr
  addq    4,      $acr

;; Bank 1 and bank 2 have the same layout, hence the loop.
  addq    1, $r1
2:
  move    $r1,  $srs
  nop
  nop
  nop
  move.d  [$acr], $r0
  move    $r0,    $s0
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s1
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s2
  addq    4,      $acr

;; S3 (MM_CAUSE) is read-only.
  addq    4,      $acr

  move.d  [$acr], $r0
  move    $r0,    $s4
  addq    4,      $acr

;; FIXME: Actually write S5/S6? (Affects MM_CAUSE.)
  addq    4,      $acr
  addq    4,      $acr

;; Nothing in S7 - S15, bank 1 and 2
  addq    4,      $acr
  addq    4,      $acr
  addq    4,      $acr
  addq    4,      $acr
  addq    4,      $acr
  addq    4,      $acr
  addq    4,      $acr
  addq    4,      $acr
  addq    4,      $acr

  addq 1, $r1
  cmpq 3, $r1
  bne 2b
  nop

;; Bank 3
  move    $r1,  $srs
  nop
  nop
  nop
  move.d  [$acr], $r0
  move    $r0,    $s0
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s1
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s2
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s3
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s4
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s5
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s6
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s7
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s8
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s9
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s10
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s11
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s12
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s13
  addq    4,      $acr
  move.d  [$acr], $r0
  move    $r0,    $s14
  addq    4,      $acr

;; Nothing in S15, bank 3
  addq    4,      $acr

;; Now, move on to the regular register restoration process.

   move.d  reg,    $acr   ; Reset ACR to point at the beginning of the register image
   move.d  [$acr], $r0    ; Restore R0
   addq    4,      $acr
   move.d  [$acr], $r1    ; Restore R1
   addq    4,      $acr
   move.d  [$acr], $r2    ; Restore R2
   addq    4,      $acr
   move.d  [$acr], $r3    ; Restore R3
   addq    4,      $acr
   move.d  [$acr], $r4    ; Restore R4
   addq    4,      $acr
   move.d  [$acr], $r5    ; Restore R5
   addq    4,      $acr
   move.d  [$acr], $r6    ; Restore R6
   addq    4,      $acr
   move.d  [$acr], $r7    ; Restore R7
   addq    4,      $acr
   move.d  [$acr], $r8    ; Restore R8
   addq    4,      $acr
   move.d  [$acr], $r9    ; Restore R9
   addq    4,      $acr
   move.d  [$acr], $r10   ; Restore R10
   addq    4,      $acr
   move.d  [$acr], $r11   ; Restore R11
   addq    4,      $acr
   move.d  [$acr], $r12   ; Restore R12
   addq    4,      $acr
   move.d  [$acr], $r13   ; Restore R13

;;
;; We restore all registers, even though some of them probably haven't changed.
;;

   addq    4,      $acr
   move.d  [$acr], $sp    ; Restore SP (R14)

   ;; ACR cannot be restored just yet.
   addq    8,      $acr

   ;; Skip BZ, VR.
   addq    2,      $acr

   move    [$acr], $pid   ; Restore PID
   addq    4,      $acr
   move    [$acr], $srs   ; Restore SRS
   nop
   nop
   nop
   addq    1,      $acr

   ;; Skip WZ.
   addq    2,      $acr

   move    [$acr], $exs    ; Restore EXS.
   addq    4,      $acr
   move    [$acr], $eda    ; Restore EDA.
   addq    4,      $acr
   move    [$acr], $mof    ; Restore MOF.

   ;; Skip DZ.
   addq    8,      $acr

   move    [$acr], $ebp    ; Restore EBP.
   addq    4,      $acr
   move    [$acr], $erp    ; Restore ERP.
   addq    4,      $acr
   move    [$acr], $srp    ; Restore SRP.
   addq    4,      $acr
   move    [$acr], $nrp    ; Restore NRP.
   addq    4,      $acr
   move    [$acr], $ccs    ; Restore CCS like an ordinary register.
   addq    4,      $acr
   move    [$acr], $usp    ; Restore USP
   addq    4,      $acr
   move    [$acr], $spc    ; Restore SPC
                           ; No restoration of pseudo-PC of course.

   move.d  reg,    $acr    ; Reset ACR to point at the beginning of the register image
   add.d   15*4,   $acr
   move.d  [$acr], $acr    ; Finally, restore ACR.
   rete                    ; Same as jump ERP
   rfe                     ; Shifts CCS
