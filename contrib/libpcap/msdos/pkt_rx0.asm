PAGE 60,132
NAME PKT_RX

ifdef ??version        ; using TASM
  masm
  jumps
endif

PUBLIC _pktDrop, _pktRxBuf, _pktTxBuf,    _pktTemp
PUBLIC _rxOutOfs, _rxInOfs, _PktReceiver, _pktRxEnd

;
; these sizes MUST be equal to the sizes in PKTDRVR.H
;

RX_BUF_SIZE = 1500      ; max message size on Ethernet
TX_BUF_SIZE = 1500

ifdef DOSX
 .386
  NUM_RX_BUF = 32       ; # of RX element buffers
  _TEXT   SEGMENT PUBLIC DWORD USE16 'CODE'
  _TEXT   ENDS
  _DATA   SEGMENT PUBLIC DWORD USE16 'CODE'
  _DATA   ENDS
  D_SEG   EQU <_TEXT SEGMENT>
  D_END   EQU <_TEXT ENDS>
  ASSUME  CS:_TEXT,DS:_TEXT
else
 .286
  NUM_RX_BUF = 10
  _TEXT   SEGMENT PUBLIC DWORD 'CODE'
  _TEXT   ENDS
  _DATA   SEGMENT PUBLIC DWORD 'DATA'
  _DATA   ENDS
  D_SEG   EQU <_DATA SEGMENT>
  D_END   EQU <_DATA ENDS>
  ASSUME  CS:_TEXT,DS:_DATA
endif

;-------------------------------------------

D_SEG

RX_ELEMENT     STRUC
   firstCount  dw  0                          ; # of bytes on 1st call
   secondCount dw  0                          ; # of bytes on 2nd call
   handle      dw  0                          ; handle for upcall
   destinAdr   db  6           dup (0)        ; packet destination address
   sourceAdr   db  6           dup (0)        ; packet source address
   protocol    dw  0                          ; packet protocol number
   rxBuffer    db  RX_BUF_SIZE dup (0)        ; RX buffer
ENDS
               align 4
_rxOutOfs      dw  offset _pktRxBuf           ; ring buffer offsets
_rxInOfs       dw  offset _pktRxBuf           ; into _pktRxBuf
_pktDrop       dw  0,0                        ; packet drop counter
_pktTemp       db  20                dup (0)  ; temp work area
_pktTxBuf      db  (TX_BUF_SIZE+14)  dup (0)  ; TX buffer
_pktRxBuf      RX_ELEMENT NUM_RX_BUF dup (<>) ; RX structures
 LAST_OFS      = offset $

 screenSeg     dw  0B800h
 newInOffset   dw  0

 fanChars      db  '-\|/'
 fanIndex      dw  0

D_END

_TEXT SEGMENT


SHOW_RX  MACRO
         push es
         push bx
         mov bx, screenSeg
         mov es, bx                    ;; r-mode segment of colour screen
         mov di, 158                   ;; upper right corner - 1
         mov bx, fanIndex
         mov al, fanChars[bx]          ;; get write char
         mov ah, 15                    ;;  and white colour
         stosw                         ;; write to screen at ES:EDI
         inc fanIndex                  ;; update next index
         and fanIndex, 3
         pop bx
         pop es
ENDM

;------------------------------------------------------------------------
;
; This macro return ES:DI to tail of Rx queue

ENQUEUE  MACRO
         LOCAL @noWrap
         mov ax, _rxInOfs              ;; DI = current in-offset
         add ax, SIZE RX_ELEMENT       ;; point to next _pktRxBuf buffer
         cmp ax, LAST_OFS              ;; pointing past last ?
         jb  @noWrap                   ;; no - jump
         lea ax, _pktRxBuf             ;; yes, point to 1st buffer
         align 4
@noWrap: cmp ax, _rxOutOfs             ;; in-ofs = out-ofs ?
         je  @dump                     ;; yes, queue is full
         mov di, _rxInOfs              ;; ES:DI -> buffer at queue input
         mov newInOffset, ax           ;; remember new input offset

   ;; NOTE. rxInOfs is updated after the packet has been copied
   ;; to ES:DI (= DS:SI on 2nd call) by the packet driver

ENDM

;------------------------------------------------------------------------
;
; This routine gets called by the packet driver twice:
;   1st time (AX=0) it requests an address where to put the packet
;
;   2nd time (AX=1) the packet has been copied to this location (DS:SI)
;   BX has client handle (stored in RX_ELEMENT.handle).
;   CX has # of bytes in packet on both call. They should be equal.
;
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
ifdef DOSX
         mov bx, cs
else
         mov bx, SEG _DATA
endif
         mov ds, bx
         mov es, bx                  ; ES = DS = CS or seg _DATA
         pop bx                      ; restore handle

         cmp ax, 0                   ; first call? (AX=0)
         jne @post                   ; AX=1: second call, do post process

ifdef DEBUG
         SHOW_RX                     ; show that a packet is received
endif
         cmp cx, RX_BUF_SIZE+14      ; size OK ?
         ja  @skip                   ; no, packet to large for us

         ENQUEUE                     ; ES:DI -> _pktRxBuf[n]

         mov [di].firstCount, cx     ; remember the first count.
         mov [di].handle, bx         ; remember the handle.
         add di, 6                   ; ES:DI -> _pktRxBuf[n].destinAdr
         pop ds
         popf
         retf                        ; far return to driver with ES:DI

         align 4
@dump:   inc _pktDrop[0]             ; discard the packet on 1st call
         adc _pktDrop[2], 0          ; increment packets lost

@skip:   xor di, di                  ; return ES:DI = NIL pointer
         xor ax, ax
         mov es, ax
         pop ds
         popf
         retf

         align 4
@post:   or si, si                   ; DS:SI->_pktRxBuf[n][n].destinAdr
         jz @discard                 ; make sure we don't use NULL-pointer

         sub si, 6                   ; DS:SI -> _pktRxBuf[n].destinAdr
       ;
       ; push si
       ; push [si].firstCount
       ; call bpf_filter_match       ; run the filter here some day?
       ; add sp, 4
       ; cmp ax, 0
       ; je  @discard

         mov [si].secondCount, cx
         mov ax, newInOffset
         mov _rxInOfs, ax            ; update _pktRxBuf input offset

         align 4
@discard:pop ds
         popf
         retf

_pktRxEnd  db 0                      ; marker for end of r-mode code/data

_TEXT ENDS

END
