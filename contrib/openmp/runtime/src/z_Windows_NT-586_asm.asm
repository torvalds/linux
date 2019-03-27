;  z_Windows_NT-586_asm.asm:  - microtasking routines specifically
;    written for IA-32 architecture and Intel(R) 64 running Windows* OS

;
;//===----------------------------------------------------------------------===//
;//
;//                     The LLVM Compiler Infrastructure
;//
;// This file is dual licensed under the MIT and the University of Illinois Open
;// Source Licenses. See LICENSE.txt for details.
;//
;//===----------------------------------------------------------------------===//
;

        TITLE   z_Windows_NT-586_asm.asm

; ============================= IA-32 architecture ==========================
ifdef _M_IA32

        .586P

if @Version gt 510
        .model HUGE
else
_TEXT   SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT   ENDS
_DATA   SEGMENT DWORD USE32 PUBLIC 'DATA'
_DATA   ENDS
CONST   SEGMENT DWORD USE32 PUBLIC 'CONST'
CONST   ENDS
_BSS    SEGMENT DWORD USE32 PUBLIC 'BSS'
_BSS    ENDS
$$SYMBOLS       SEGMENT BYTE USE32 'DEBSYM'
$$SYMBOLS       ENDS
$$TYPES SEGMENT BYTE USE32 'DEBTYP'
$$TYPES ENDS
_TLS    SEGMENT DWORD USE32 PUBLIC 'TLS'
_TLS    ENDS
FLAT    GROUP _DATA, CONST, _BSS
        ASSUME  CS: FLAT, DS: FLAT, SS: FLAT
endif


;------------------------------------------------------------------------
; FUNCTION ___kmp_x86_pause
;
; void
; __kmp_x86_pause( void )
PUBLIC  ___kmp_x86_pause
_p$ = 4
_d$ = 8
_TEXT   SEGMENT
        ALIGN 16
___kmp_x86_pause PROC NEAR

        db      0f3H
        db      090H    ;; pause
        ret

___kmp_x86_pause ENDP
_TEXT   ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_x86_cpuid
;
; void
; __kmp_x86_cpuid( int mode, int mode2, struct kmp_cpuid *p );
PUBLIC  ___kmp_x86_cpuid
_TEXT   SEGMENT
        ALIGN 16
_mode$  = 8
_mode2$ = 12
_p$     = 16
_eax$   = 0
_ebx$   = 4
_ecx$   = 8
_edx$   = 12

___kmp_x86_cpuid PROC NEAR

        push      ebp
        mov       ebp, esp

        push      edi
        push      ebx
        push      ecx
        push      edx

        mov	  eax, DWORD PTR _mode$[ebp]
        mov	  ecx, DWORD PTR _mode2$[ebp]
	cpuid					; Query the CPUID for the current processor

        mov       edi, DWORD PTR _p$[ebp]
	mov 	  DWORD PTR _eax$[ edi ], eax
	mov 	  DWORD PTR _ebx$[ edi ], ebx
	mov 	  DWORD PTR _ecx$[ edi ], ecx
	mov 	  DWORD PTR _edx$[ edi ], edx

        pop       edx
        pop       ecx
        pop       ebx
        pop       edi

        mov       esp, ebp
        pop       ebp
        ret

___kmp_x86_cpuid ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_test_then_add32
;
; kmp_int32
; __kmp_test_then_add32( volatile kmp_int32 *p, kmp_int32 d );
PUBLIC  ___kmp_test_then_add32
_p$ = 4
_d$ = 8
_TEXT   SEGMENT
        ALIGN 16
___kmp_test_then_add32 PROC NEAR

        mov     eax, DWORD PTR _d$[esp]
        mov     ecx, DWORD PTR _p$[esp]
lock    xadd    DWORD PTR [ecx], eax
        ret

___kmp_test_then_add32 ENDP
_TEXT   ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_compare_and_store8
;
; kmp_int8
; __kmp_compare_and_store8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
PUBLIC  ___kmp_compare_and_store8
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_cv$ = 8
_sv$ = 12

___kmp_compare_and_store8 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       al, BYTE PTR _cv$[esp]
        mov       dl, BYTE PTR _sv$[esp]
lock    cmpxchg   BYTE PTR [ecx], dl
        sete      al           ; if al == [ecx] set al = 1 else set al = 0
        and       eax, 1       ; sign extend previous instruction
        ret

___kmp_compare_and_store8 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_compare_and_store16
;
; kmp_int16
; __kmp_compare_and_store16( volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv );
PUBLIC  ___kmp_compare_and_store16
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_cv$ = 8
_sv$ = 12

___kmp_compare_and_store16 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       ax, WORD PTR _cv$[esp]
        mov       dx, WORD PTR _sv$[esp]
lock    cmpxchg   WORD PTR [ecx], dx
        sete      al           ; if ax == [ecx] set al = 1 else set al = 0
        and       eax, 1       ; sign extend previous instruction
        ret

___kmp_compare_and_store16 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_compare_and_store32
;
; kmp_int32
; __kmp_compare_and_store32( volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv );
PUBLIC  ___kmp_compare_and_store32
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_cv$ = 8
_sv$ = 12

___kmp_compare_and_store32 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       eax, DWORD PTR _cv$[esp]
        mov       edx, DWORD PTR _sv$[esp]
lock    cmpxchg   DWORD PTR [ecx], edx
        sete      al           ; if eax == [ecx] set al = 1 else set al = 0
        and       eax, 1       ; sign extend previous instruction
        ret

___kmp_compare_and_store32 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_compare_and_store64
;
; kmp_int32
; __kmp_compare_and_store64( volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 sv );
PUBLIC  ___kmp_compare_and_store64
_TEXT   SEGMENT
        ALIGN 16
_p$ = 8
_cv_low$ = 12
_cv_high$ = 16
_sv_low$ = 20
_sv_high$ = 24

___kmp_compare_and_store64 PROC NEAR

        push      ebp
        mov       ebp, esp
        push      ebx
        push      edi
        mov       edi, DWORD PTR _p$[ebp]
        mov       eax, DWORD PTR _cv_low$[ebp]
        mov       edx, DWORD PTR _cv_high$[ebp]
        mov       ebx, DWORD PTR _sv_low$[ebp]
        mov       ecx, DWORD PTR _sv_high$[ebp]
lock    cmpxchg8b QWORD PTR [edi]
        sete      al           ; if edx:eax == [edi] set al = 1 else set al = 0
        and       eax, 1       ; sign extend previous instruction
        pop       edi
        pop       ebx
        mov       esp, ebp
        pop       ebp
        ret

___kmp_compare_and_store64 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_xchg_fixed8
;
; kmp_int8
; __kmp_xchg_fixed8( volatile kmp_int8 *p, kmp_int8 d );
PUBLIC  ___kmp_xchg_fixed8
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_d$ = 8

___kmp_xchg_fixed8 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       al,  BYTE PTR _d$[esp]
lock    xchg      BYTE PTR [ecx], al
        ret

___kmp_xchg_fixed8 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_xchg_fixed16
;
; kmp_int16
; __kmp_xchg_fixed16( volatile kmp_int16 *p, kmp_int16 d );
PUBLIC  ___kmp_xchg_fixed16
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_d$ = 8

___kmp_xchg_fixed16 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       ax,  WORD PTR  _d$[esp]
lock    xchg      WORD PTR [ecx], ax
        ret

___kmp_xchg_fixed16 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_xchg_fixed32
;
; kmp_int32
; __kmp_xchg_fixed32( volatile kmp_int32 *p, kmp_int32 d );
PUBLIC  ___kmp_xchg_fixed32
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_d$ = 8

___kmp_xchg_fixed32 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       eax, DWORD PTR _d$[esp]
lock    xchg      DWORD PTR [ecx], eax
        ret

___kmp_xchg_fixed32 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION ___kmp_xchg_real32
;
; kmp_real32
; __kmp_xchg_real32( volatile kmp_real32 *p, kmp_real32 d );
PUBLIC  ___kmp_xchg_real32
_TEXT   SEGMENT
        ALIGN 16
_p$ = 8
_d$ = 12
_old_value$ = -4

___kmp_xchg_real32 PROC NEAR

        push    ebp
        mov     ebp, esp
        sub     esp, 4
        push    esi
        mov     esi, DWORD PTR _p$[ebp]

        fld     DWORD PTR [esi]
                        ;; load <addr>
        fst     DWORD PTR _old_value$[ebp]
                        ;; store into old_value

        mov     eax, DWORD PTR _d$[ebp]

lock    xchg    DWORD PTR [esi], eax

        fld     DWORD PTR _old_value$[ebp]
                        ;; return old_value
        pop     esi
        mov     esp, ebp
        pop     ebp
        ret

___kmp_xchg_real32 ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION ___kmp_compare_and_store_ret8
;
; kmp_int8
; __kmp_compare_and_store_ret8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
PUBLIC  ___kmp_compare_and_store_ret8
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_cv$ = 8
_sv$ = 12

___kmp_compare_and_store_ret8 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       al, BYTE PTR _cv$[esp]
        mov       dl, BYTE PTR _sv$[esp]
lock    cmpxchg   BYTE PTR [ecx], dl
        ret

___kmp_compare_and_store_ret8 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_compare_and_store_ret16
;
; kmp_int16
; __kmp_compare_and_store_ret16( volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv );
PUBLIC  ___kmp_compare_and_store_ret16
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_cv$ = 8
_sv$ = 12

___kmp_compare_and_store_ret16 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       ax, WORD PTR _cv$[esp]
        mov       dx, WORD PTR _sv$[esp]
lock    cmpxchg   WORD PTR [ecx], dx
        ret

___kmp_compare_and_store_ret16 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_compare_and_store_ret32
;
; kmp_int32
; __kmp_compare_and_store_ret32( volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv );
PUBLIC  ___kmp_compare_and_store_ret32
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4
_cv$ = 8
_sv$ = 12

___kmp_compare_and_store_ret32 PROC NEAR

        mov       ecx, DWORD PTR _p$[esp]
        mov       eax, DWORD PTR _cv$[esp]
        mov       edx, DWORD PTR _sv$[esp]
lock    cmpxchg   DWORD PTR [ecx], edx
        ret

___kmp_compare_and_store_ret32 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_compare_and_store_ret64
;
; kmp_int64
; __kmp_compare_and_store_ret64( volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 sv );
PUBLIC  ___kmp_compare_and_store_ret64
_TEXT   SEGMENT
        ALIGN 16
_p$ = 8
_cv_low$ = 12
_cv_high$ = 16
_sv_low$ = 20
_sv_high$ = 24

___kmp_compare_and_store_ret64 PROC NEAR

        push      ebp
        mov       ebp, esp
        push      ebx
        push      edi
        mov       edi, DWORD PTR _p$[ebp]
        mov       eax, DWORD PTR _cv_low$[ebp]
        mov       edx, DWORD PTR _cv_high$[ebp]
        mov       ebx, DWORD PTR _sv_low$[ebp]
        mov       ecx, DWORD PTR _sv_high$[ebp]
lock    cmpxchg8b QWORD PTR [edi]
        pop       edi
        pop       ebx
        mov       esp, ebp
        pop       ebp
        ret

___kmp_compare_and_store_ret64 ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_load_x87_fpu_control_word
;
; void
; __kmp_load_x87_fpu_control_word( kmp_int16 *p );
;
; parameters:
;       p:      4(%esp)
PUBLIC  ___kmp_load_x87_fpu_control_word
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4

___kmp_load_x87_fpu_control_word PROC NEAR

        mov       eax, DWORD PTR _p$[esp]
        fldcw     WORD PTR [eax]
        ret

___kmp_load_x87_fpu_control_word ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_store_x87_fpu_control_word
;
; void
; __kmp_store_x87_fpu_control_word( kmp_int16 *p );
;
; parameters:
;       p:      4(%esp)
PUBLIC  ___kmp_store_x87_fpu_control_word
_TEXT   SEGMENT
        ALIGN 16
_p$ = 4

___kmp_store_x87_fpu_control_word PROC NEAR

        mov       eax, DWORD PTR _p$[esp]
        fstcw     WORD PTR [eax]
        ret

___kmp_store_x87_fpu_control_word ENDP
_TEXT     ENDS

;------------------------------------------------------------------------
; FUNCTION ___kmp_clear_x87_fpu_status_word
;
; void
; __kmp_clear_x87_fpu_status_word();
PUBLIC  ___kmp_clear_x87_fpu_status_word
_TEXT   SEGMENT
        ALIGN 16

___kmp_clear_x87_fpu_status_word PROC NEAR

        fnclex
        ret

___kmp_clear_x87_fpu_status_word ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION ___kmp_invoke_microtask
;
; typedef void  (*microtask_t)( int *gtid, int *tid, ... );
;
; int
; __kmp_invoke_microtask( microtask_t pkfn,
;                         int gtid, int tid,
;                         int argc, void *p_argv[] )
PUBLIC  ___kmp_invoke_microtask
_TEXT   SEGMENT
        ALIGN 16
_pkfn$ = 8
_gtid$ = 12
_tid$ = 16
_argc$ = 20
_argv$ = 24
if OMPT_SUPPORT
_exit_frame$ = 28
endif
_i$ = -8
_stk_adj$ = -16
_vptr$ = -12
_qptr$ = -4

___kmp_invoke_microtask PROC NEAR
; Line 102
        push    ebp
        mov     ebp, esp
        sub     esp, 16                                 ; 00000010H
        push    ebx
        push    esi
        push    edi
if OMPT_SUPPORT
        mov     eax, DWORD PTR _exit_frame$[ebp]
        mov     DWORD PTR [eax], ebp
endif
; Line 114
        mov     eax, DWORD PTR _argc$[ebp]
        mov     DWORD PTR _i$[ebp], eax

;; ------------------------------------------------------------
	lea     edx, DWORD PTR [eax*4+8]
	mov     ecx, esp                                ; Save current SP into ECX
	mov	eax,edx		; Save the size of the args in eax
	sub	ecx,edx		; esp-((#args+2)*4) -> ecx -- without mods, stack ptr would be this
	mov	edx,ecx		; Save to edx
	and	ecx,-128	; Mask off 7 bits
	sub	edx,ecx		; Amount to subtract from esp
	sub	esp,edx		; Prepare stack ptr-- Now it will be aligned on 128-byte boundary at the call

	add	edx,eax		; Calculate total size of the stack decrement.
        mov     DWORD PTR _stk_adj$[ebp], edx
;; ------------------------------------------------------------

        jmp     SHORT $L22237
$L22238:
        mov     ecx, DWORD PTR _i$[ebp]
        sub     ecx, 1
        mov     DWORD PTR _i$[ebp], ecx
$L22237:
        cmp     DWORD PTR _i$[ebp], 0
        jle     SHORT $L22239
; Line 116
        mov     edx, DWORD PTR _i$[ebp]
        mov     eax, DWORD PTR _argv$[ebp]
        mov     ecx, DWORD PTR [eax+edx*4-4]
        mov     DWORD PTR _vptr$[ebp], ecx
; Line 123
        mov     eax, DWORD PTR _vptr$[ebp]
; Line 124
        push    eax
; Line 127
        jmp     SHORT $L22238
$L22239:
; Line 129
        lea     edx, DWORD PTR _tid$[ebp]
        mov     DWORD PTR _vptr$[ebp], edx
; Line 130
        lea     eax, DWORD PTR _gtid$[ebp]
        mov     DWORD PTR _qptr$[ebp], eax
; Line 143
        mov     eax, DWORD PTR _vptr$[ebp]
; Line 144
        push    eax
; Line 145
        mov     eax, DWORD PTR _qptr$[ebp]
; Line 146
        push    eax
; Line 147
        call    DWORD PTR _pkfn$[ebp]
; Line 148
        add     esp, DWORD PTR _stk_adj$[ebp]
; Line 152
        mov     eax, 1
; Line 153
        pop     edi
        pop     esi
        pop     ebx
        mov     esp, ebp
        pop     ebp
        ret     0
___kmp_invoke_microtask ENDP
_TEXT   ENDS

endif

; ==================================== Intel(R) 64 ===================================

ifdef _M_AMD64

;------------------------------------------------------------------------
; FUNCTION __kmp_x86_cpuid
;
; void
; __kmp_x86_cpuid( int mode, int mode2, struct kmp_cpuid *p );
;
; parameters:
;	mode:		ecx
;	mode2:		edx
;	cpuid_buffer: 	r8
PUBLIC  __kmp_x86_cpuid
_TEXT   SEGMENT
        ALIGN 16

__kmp_x86_cpuid PROC FRAME ;NEAR

        push      rbp
        .pushreg  rbp
        mov       rbp, rsp
        .setframe rbp, 0
        push      rbx				; callee-save register
        .pushreg  rbx
        .ENDPROLOG

	mov	  r10, r8                       ; p parameter
        mov	  eax, ecx			; mode parameter
        mov	  ecx, edx                      ; mode2 parameter
	cpuid					; Query the CPUID for the current processor

	mov 	  DWORD PTR 0[ r10 ], eax	; store results into buffer
	mov 	  DWORD PTR 4[ r10 ], ebx
	mov 	  DWORD PTR 8[ r10 ], ecx
	mov 	  DWORD PTR 12[ r10 ], edx

        pop       rbx				; callee-save register
        mov       rsp, rbp
        pop       rbp
        ret

__kmp_x86_cpuid ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_test_then_add32
;
; kmp_int32
; __kmp_test_then_add32( volatile kmp_int32 *p, kmp_int32 d );
;
; parameters:
;	p:	rcx
;	d:	edx
;
; return: 	eax
PUBLIC  __kmp_test_then_add32
_TEXT   SEGMENT
        ALIGN 16
__kmp_test_then_add32 PROC ;NEAR

        mov     eax, edx
lock    xadd    DWORD PTR [rcx], eax
        ret

__kmp_test_then_add32 ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_test_then_add64
;
; kmp_int32
; __kmp_test_then_add64( volatile kmp_int64 *p, kmp_int64 d );
;
; parameters:
;	p:	rcx
;	d:	rdx
;
; return: 	rax
PUBLIC  __kmp_test_then_add64
_TEXT   SEGMENT
        ALIGN 16
__kmp_test_then_add64 PROC ;NEAR

        mov     rax, rdx
lock    xadd    QWORD PTR [rcx], rax
        ret

__kmp_test_then_add64 ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store8
;
; kmp_int8
; __kmp_compare_and_store8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
; parameters:
;	p:	rcx
;	cv:	edx
;	sv:	r8d
;
; return:	eax
PUBLIC  __kmp_compare_and_store8
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store8 PROC ;NEAR

        mov       al, dl	; "cv"
	mov	  edx, r8d	; "sv"
lock    cmpxchg   BYTE PTR [rcx], dl
        sete      al           	; if al == [rcx] set al = 1 else set al = 0
        and       rax, 1       	; sign extend previous instruction
        ret

__kmp_compare_and_store8 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store16
;
; kmp_int16
; __kmp_compare_and_store16( volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv );
; parameters:
;	p:	rcx
;	cv:	edx
;	sv:	r8d
;
; return:	eax
PUBLIC  __kmp_compare_and_store16
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store16 PROC ;NEAR

        mov       ax, dx	; "cv"
	mov	  edx, r8d	; "sv"
lock    cmpxchg   WORD PTR [rcx], dx
        sete      al           	; if ax == [rcx] set al = 1 else set al = 0
        and       rax, 1       	; sign extend previous instruction
        ret

__kmp_compare_and_store16 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store32
;
; kmp_int32
; __kmp_compare_and_store32( volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv );
; parameters:
;	p:	rcx
;	cv:	edx
;	sv:	r8d
;
; return:	eax
PUBLIC  __kmp_compare_and_store32
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store32 PROC ;NEAR

        mov       eax, edx	; "cv"
	mov	  edx, r8d	; "sv"
lock    cmpxchg   DWORD PTR [rcx], edx
        sete      al           	; if eax == [rcx] set al = 1 else set al = 0
        and       rax, 1       	; sign extend previous instruction
        ret

__kmp_compare_and_store32 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store64
;
; kmp_int32
; __kmp_compare_and_store64( volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 sv );
; parameters:
;	p:	rcx
;	cv:	rdx
;	sv:	r8
;
; return:	eax
PUBLIC  __kmp_compare_and_store64
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store64 PROC ;NEAR

        mov       rax, rdx	; "cv"
	mov	  rdx, r8	; "sv"
lock    cmpxchg   QWORD PTR [rcx], rdx
        sete      al           ; if rax == [rcx] set al = 1 else set al = 0
        and       rax, 1       ; sign extend previous instruction
        ret

__kmp_compare_and_store64 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION ___kmp_xchg_fixed8
;
; kmp_int8
; __kmp_xchg_fixed8( volatile kmp_int8 *p, kmp_int8 d );
;
; parameters:
;	p:	rcx
;	d:	dl
;
; return: 	al
PUBLIC  __kmp_xchg_fixed8
_TEXT   SEGMENT
        ALIGN 16

__kmp_xchg_fixed8 PROC ;NEAR

        mov       al,  dl
lock    xchg      BYTE PTR [rcx], al
        ret

__kmp_xchg_fixed8 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION ___kmp_xchg_fixed16
;
; kmp_int16
; __kmp_xchg_fixed16( volatile kmp_int16 *p, kmp_int16 d );
;
; parameters:
;	p:	rcx
;	d:	dx
;
; return: 	ax
PUBLIC  __kmp_xchg_fixed16
_TEXT   SEGMENT
        ALIGN 16

__kmp_xchg_fixed16 PROC ;NEAR

        mov       ax,  dx
lock    xchg      WORD PTR [rcx], ax
        ret

__kmp_xchg_fixed16 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION ___kmp_xchg_fixed32
;
; kmp_int32
; __kmp_xchg_fixed32( volatile kmp_int32 *p, kmp_int32 d );
;
; parameters:
;	p:	rcx
;	d:	edx
;
; return: 	eax
PUBLIC  __kmp_xchg_fixed32
_TEXT   SEGMENT
        ALIGN 16
__kmp_xchg_fixed32 PROC ;NEAR

        mov     eax, edx
lock    xchg    DWORD PTR [rcx], eax
        ret

__kmp_xchg_fixed32 ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION ___kmp_xchg_fixed64
;
; kmp_int64
; __kmp_xchg_fixed64( volatile kmp_int64 *p, kmp_int64 d );
;
; parameters:
;	p:	rcx
;	d:	rdx
;
; return: 	rax
PUBLIC  __kmp_xchg_fixed64
_TEXT   SEGMENT
        ALIGN 16
__kmp_xchg_fixed64 PROC ;NEAR

        mov     rax, rdx
lock    xchg    QWORD PTR [rcx], rax
        ret

__kmp_xchg_fixed64 ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store_ret8
;
; kmp_int8
; __kmp_compare_and_store_ret8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
; parameters:
;	p:	rcx
;	cv:	edx
;	sv:	r8d
;
; return:	eax
PUBLIC  __kmp_compare_and_store_ret8
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store_ret8 PROC ;NEAR
        mov       al, dl	; "cv"
	mov	  edx, r8d	; "sv"
lock    cmpxchg   BYTE PTR [rcx], dl
                        ; Compare AL with [rcx].  If equal set
                        ; ZF and exchange DL with [rcx].  Else, clear
                        ; ZF and load [rcx] into AL.
        ret

__kmp_compare_and_store_ret8 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store_ret16
;
; kmp_int16
; __kmp_compare_and_store_ret16( volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv );
; parameters:
;	p:	rcx
;	cv:	edx
;	sv:	r8d
;
; return:	eax
PUBLIC  __kmp_compare_and_store_ret16
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store_ret16 PROC ;NEAR

        mov       ax, dx	; "cv"
	mov	  edx, r8d	; "sv"
lock    cmpxchg   WORD PTR [rcx], dx
        ret

__kmp_compare_and_store_ret16 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store_ret32
;
; kmp_int32
; __kmp_compare_and_store_ret32( volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv );
; parameters:
;	p:	rcx
;	cv:	edx
;	sv:	r8d
;
; return:	eax
PUBLIC  __kmp_compare_and_store_ret32
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store_ret32 PROC ;NEAR

        mov       eax, edx	; "cv"
	mov	  edx, r8d	; "sv"
lock    cmpxchg   DWORD PTR [rcx], edx
        ret

__kmp_compare_and_store_ret32 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store_ret64
;
; kmp_int64
; __kmp_compare_and_store_ret64( volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 sv );
; parameters:
;	p:	rcx
;	cv:	rdx
;	sv:	r8
;
; return:	rax
PUBLIC  __kmp_compare_and_store_ret64
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store_ret64 PROC ;NEAR

        mov       rax, rdx	; "cv"
	mov	  rdx, r8	; "sv"
lock    cmpxchg   QWORD PTR [rcx], rdx
        ret

__kmp_compare_and_store_ret64 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_compare_and_store_loop8
;
; kmp_int8
; __kmp_compare_and_store_loop8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
; parameters:
;	p:	rcx
;	cv:	edx
;	sv:	r8d
;
; return:	al
PUBLIC  __kmp_compare_and_store_loop8
_TEXT   SEGMENT
        ALIGN 16

__kmp_compare_and_store_loop8 PROC ;NEAR
$__kmp_loop:
        mov       al, dl	; "cv"
	mov	  edx, r8d	; "sv"
lock    cmpxchg   BYTE PTR [rcx], dl
                        ; Compare AL with [rcx].  If equal set
                        ; ZF and exchange DL with [rcx].  Else, clear
                        ; ZF and load [rcx] into AL.
        jz     	SHORT $__kmp_success

        db      0f3H
        db      090H    		; pause

	jmp	SHORT $__kmp_loop

$__kmp_success:
        ret

__kmp_compare_and_store_loop8 ENDP
_TEXT     ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_xchg_real32
;
; kmp_real32
; __kmp_xchg_real32( volatile kmp_real32 *p, kmp_real32 d );
;
; parameters:
;	p:	rcx
;       d:	xmm1 (lower 4 bytes)
;
; return:	xmm0 (lower 4 bytes)
PUBLIC  __kmp_xchg_real32
_TEXT   SEGMENT
        ALIGN 16
__kmp_xchg_real32 PROC ;NEAR

	movd	eax, xmm1		; load d

lock    xchg    DWORD PTR [rcx], eax

	movd	xmm0, eax		; load old value into return register
        ret

__kmp_xchg_real32 ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_xchg_real64
;
; kmp_real64
; __kmp_xchg_real64( volatile kmp_real64 *p, kmp_real64 d );
;
; parameters:
;	p:	rcx
;	d:	xmm1 (lower 8 bytes)
;
; return:	xmm0 (lower 8 bytes)
PUBLIC  __kmp_xchg_real64
_TEXT   SEGMENT
        ALIGN 16
__kmp_xchg_real64 PROC ;NEAR

	movd	rax, xmm1		; load "d"

lock    xchg    QWORD PTR [rcx], rax

	movd	xmm0, rax		; load old value into return register
        ret

__kmp_xchg_real64 ENDP
_TEXT   ENDS

;------------------------------------------------------------------------
; FUNCTION __kmp_load_x87_fpu_control_word
;
; void
; __kmp_load_x87_fpu_control_word( kmp_int16 *p );
;
; parameters:
;	p:	rcx
PUBLIC  __kmp_load_x87_fpu_control_word
_TEXT   SEGMENT
        ALIGN 16
__kmp_load_x87_fpu_control_word PROC ;NEAR

        fldcw   WORD PTR [rcx]
        ret

__kmp_load_x87_fpu_control_word ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_store_x87_fpu_control_word
;
; void
; __kmp_store_x87_fpu_control_word( kmp_int16 *p );
;
; parameters:
;	p:	rcx
PUBLIC  __kmp_store_x87_fpu_control_word
_TEXT   SEGMENT
        ALIGN 16
__kmp_store_x87_fpu_control_word PROC ;NEAR

        fstcw   WORD PTR [rcx]
        ret

__kmp_store_x87_fpu_control_word ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_clear_x87_fpu_status_word
;
; void
; __kmp_clear_x87_fpu_status_word()
PUBLIC  __kmp_clear_x87_fpu_status_word
_TEXT   SEGMENT
        ALIGN 16
__kmp_clear_x87_fpu_status_word PROC ;NEAR

        fnclex
        ret

__kmp_clear_x87_fpu_status_word ENDP
_TEXT   ENDS


;------------------------------------------------------------------------
; FUNCTION __kmp_invoke_microtask
;
; typedef void  (*microtask_t)( int *gtid, int *tid, ... );
;
; int
; __kmp_invoke_microtask( microtask_t pkfn,
;                         int gtid, int tid,
;                         int argc, void *p_argv[] ) {
;
;     (*pkfn) ( &gtid, &tid, argv[0], ... );
;     return 1;
; }
;
; note:
;      just before call to pkfn must have rsp 128-byte aligned for compiler
;
; parameters:
;      rcx:   pkfn	16[rbp]
;      edx:   gtid	24[rbp]
;      r8d:   tid	32[rbp]
;      r9d:   argc	40[rbp]
;      [st]:  p_argv	48[rbp]
;
; reg temps:
;      rax:   used all over the place
;      rdx:   used all over the place
;      rcx:   used as argument counter for push parms loop
;      r10:   used to hold pkfn function pointer argument
;
; return:      eax    (always 1/TRUE)
$_pkfn   = 16
$_gtid   = 24
$_tid    = 32
$_argc   = 40
$_p_argv = 48
if OMPT_SUPPORT
$_exit_frame = 56
endif

PUBLIC  __kmp_invoke_microtask
_TEXT   SEGMENT
        ALIGN 16

__kmp_invoke_microtask PROC FRAME ;NEAR
	mov	QWORD PTR 16[rsp], rdx	; home gtid parameter
	mov 	QWORD PTR 24[rsp], r8	; home tid parameter
        push    rbp		; save base pointer
        .pushreg rbp
	sub	rsp, 0		; no fixed allocation necessary - end prolog

        lea     rbp, QWORD PTR [rsp]   	; establish the base pointer
        .setframe rbp, 0
        .ENDPROLOG
if OMPT_SUPPORT
        mov     rax, QWORD PTR $_exit_frame[rbp]
        mov     QWORD PTR [rax], rbp
endif
	mov	r10, rcx	; save pkfn pointer for later

;; ------------------------------------------------------------
        mov     rax, r9		; rax <= argc
        cmp     rax, 2
        jge     SHORT $_kmp_invoke_stack_align
        mov     rax, 2          ; set 4 homes if less than 2 parms
$_kmp_invoke_stack_align:
	lea     rdx, QWORD PTR [rax*8+16] ; rax <= (argc + 2) * 8
	mov     rax, rsp        ; Save current SP into rax
	sub	rax, rdx	; rsp - ((argc+2)*8) -> rax
				; without align, rsp would be this
	and     rax, -128       ; Mask off 7 bits (128-byte align)
	add     rax, rdx        ; add space for push's in a loop below
	mov     rsp, rax        ; Prepare the stack ptr
				; Now it will align to 128-byte at the call
;; ------------------------------------------------------------
        			; setup pkfn parameter stack
	mov	rax, r9		; rax <= argc
	shl	rax, 3		; rax <= argc*8
	mov	rdx, QWORD PTR $_p_argv[rbp]	; rdx <= p_argv
	add	rdx, rax	; rdx <= &p_argv[argc]
	mov	rcx, r9		; rcx <= argc
	jecxz	SHORT $_kmp_invoke_pass_parms	; nothing to push if argc=0
	cmp	ecx, 1		; if argc=1 branch ahead
	je	SHORT $_kmp_invoke_one_parm
	sub	ecx, 2		; if argc=2 branch ahead, subtract two from
	je	SHORT $_kmp_invoke_two_parms

$_kmp_invoke_push_parms:	; push last - 5th parms to pkfn on stack
	sub	rdx, 8		; decrement p_argv pointer to previous parm
	mov 	r8, QWORD PTR [rdx] ; r8 <= p_argv[rcx-1]
	push	r8		; push p_argv[rcx-1] onto stack (reverse order)
	sub	ecx, 1
	jecxz	SHORT $_kmp_invoke_two_parms
	jmp	SHORT $_kmp_invoke_push_parms

$_kmp_invoke_two_parms:
	sub	rdx, 8		; put 4th parm to pkfn in r9
	mov	r9, QWORD PTR [rdx] ; r9 <= p_argv[1]

$_kmp_invoke_one_parm:
        sub	rdx, 8		; put 3rd parm to pkfn in r8
	mov	r8, QWORD PTR [rdx] ; r8 <= p_argv[0]

$_kmp_invoke_pass_parms:	; put 1st & 2nd parms to pkfn in registers
	lea	rdx, QWORD PTR $_tid[rbp]  ; rdx <= &tid (2nd parm to pkfn)
	lea	rcx, QWORD PTR $_gtid[rbp] ; rcx <= &gtid (1st parm to pkfn)
        sub     rsp, 32         ; add stack space for first four parms
	mov	rax, r10	; rax <= pkfn
	call	rax		; call (*pkfn)()
	mov	rax, 1		; move 1 into return register;

        lea     rsp, QWORD PTR [rbp]	; restore stack pointer

;	add	rsp, 0		; no fixed allocation necessary - start epilog
        pop     rbp		; restore frame pointer
        ret
__kmp_invoke_microtask ENDP
_TEXT   ENDS

endif

END
