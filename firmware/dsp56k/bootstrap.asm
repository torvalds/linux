; Author: Frederik Noring <noring@nocrew.org>
;
; This file is subject to the terms and conditions of the GNU General Public
; License.  See the file COPYING in the main directory of this archive
; for more details.

; DSP56k loader

; Host Interface
M_BCR   EQU     $FFFE           ; Port A Bus Control Register
M_PBC   EQU     $FFE0           ; Port B Control Register
M_PBDDR EQU     $FFE2           ; Port B Data Direction Register
M_PBD   EQU     $FFE4           ; Port B Data Register
M_PCC   EQU     $FFE1           ; Port C Control Register
M_PCDDR EQU     $FFE3           ; Port C Data Direction Register
M_PCD   EQU     $FFE5           ; Port C Data Register

M_HCR   EQU     $FFE8           ; Host Control Register
M_HSR   EQU     $FFE9           ; Host Status Register
M_HRX   EQU     $FFEB           ; Host Receive Data Register
M_HTX   EQU     $FFEB           ; Host Transmit Data Register

; SSI, Synchronous Serial Interface
M_RX    EQU     $FFEF           ; Serial Receive Data Register
M_TX    EQU     $FFEF           ; Serial Transmit Data Register
M_CRA   EQU     $FFEC           ; SSI Control Register A
M_CRB   EQU     $FFED           ; SSI Control Register B
M_SR    EQU     $FFEE           ; SSI Status Register
M_TSR   EQU     $FFEE           ; SSI Time Slot Register

; Exception Processing
M_IPR   EQU     $FFFF           ; Interrupt Priority Register

        org     P:$0
start   jmp     <$40

        org     P:$40
;       ; Zero 16384 DSP X and Y words
;       clr     A #0,r0
;       clr     B #0,r4
;       do      #64,<_block1
;       rep     #256
;       move    A,X:(r0)+ B,Y:(r4)+
;_block1        ; Zero (32768-512) Program words
;       clr     A #512,r0
;       do      #126,<_block2
;       rep     #256
;       move    A,P:(r0)+
;_block2

        ; Copy DSP program control
        move    #real,r0
        move    #upload,r1
        do      #upload_end-upload,_copy
        movem    P:(r0)+,x0
        movem    x0,P:(r1)+
_copy   movep   #4,X:<<M_HCR
        movep   #$c00,X:<<M_IPR
        and     #<$fe,mr
        jmp     upload

real
        org     P:$7ea9
upload
        movep   #1,X:<<M_PBC
        movep   #0,X:<<M_BCR

next    jclr    #0,X:<<M_HSR,*
        movep   X:<<M_HRX,A
        move    #>3,x0
        cmp     x0,A #>1,x0
        jeq     <$0
_get_address
        jclr    #0,X:<<M_HSR,_get_address
        movep   X:<<M_HRX,r0
_get_length
        jclr    #0,X:<<M_HSR,_get_length
        movep   X:<<M_HRX,y0
        cmp     x0,A #>2,x0
        jeq     load_X
        cmp     x0,A
        jeq     load_Y

load_P  do      y0,_load_P
        jclr    #0,X:<<M_HSR,*
        movep   X:<<M_HRX,P:(r0)+
_load_P jmp     next
load_X  do      y0,_load_X
        jclr    #0,X:<<M_HSR,*
        movep   X:<<M_HRX,X:(r0)+
_load_X jmp     next
load_Y  do      y0,_load_Y
        jclr    #0,X:<<M_HSR,*
        movep   X:<<M_HRX,Y:(r0)+
_load_Y jmp     next

upload_end
        end
