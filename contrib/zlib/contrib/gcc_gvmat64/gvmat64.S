/*
;uInt longest_match_x64(
;    deflate_state *s,
;    IPos cur_match);                             // current match 

; gvmat64.S -- Asm portion of the optimized longest_match for 32 bits x86_64
;  (AMD64 on Athlon 64, Opteron, Phenom
;     and Intel EM64T on Pentium 4 with EM64T, Pentium D, Core 2 Duo, Core I5/I7)
; this file is translation from gvmat64.asm to GCC 4.x (for Linux, Mac XCode)
; Copyright (C) 1995-2010 Jean-loup Gailly, Brian Raiter and Gilles Vollant.
;
; File written by Gilles Vollant, by converting to assembly the longest_match
;  from Jean-loup Gailly in deflate.c of zLib and infoZip zip.
;  and by taking inspiration on asm686 with masm, optimised assembly code
;        from Brian Raiter, written 1998
;
;  This software is provided 'as-is', without any express or implied
;  warranty.  In no event will the authors be held liable for any damages
;  arising from the use of this software.
;
;  Permission is granted to anyone to use this software for any purpose,
;  including commercial applications, and to alter it and redistribute it
;  freely, subject to the following restrictions:
;
;  1. The origin of this software must not be misrepresented; you must not
;     claim that you wrote the original software. If you use this software
;     in a product, an acknowledgment in the product documentation would be
;     appreciated but is not required.
;  2. Altered source versions must be plainly marked as such, and must not be
;     misrepresented as being the original software
;  3. This notice may not be removed or altered from any source distribution.
;
;         http://www.zlib.net
;         http://www.winimage.com/zLibDll
;         http://www.muppetlabs.com/~breadbox/software/assembly.html
;
; to compile this file for zLib, I use option:
;   gcc -c -arch x86_64 gvmat64.S


;uInt longest_match(s, cur_match)
;    deflate_state *s;
;    IPos cur_match;                             // current match /
;
; with XCode for Mac, I had strange error with some jump on intel syntax
; this is why BEFORE_JMP and AFTER_JMP are used
 */


#define BEFORE_JMP .att_syntax
#define AFTER_JMP .intel_syntax noprefix

#ifndef NO_UNDERLINE
#	define	match_init	_match_init
#	define	longest_match	_longest_match
#endif

.intel_syntax noprefix

.globl	match_init, longest_match
.text
longest_match:



#define LocalVarsSize 96
/*
; register used : rax,rbx,rcx,rdx,rsi,rdi,r8,r9,r10,r11,r12
; free register :  r14,r15
; register can be saved : rsp
*/

#define chainlenwmask     (rsp + 8 - LocalVarsSize)
#define nicematch         (rsp + 16 - LocalVarsSize)

#define save_rdi        (rsp + 24 - LocalVarsSize)
#define save_rsi        (rsp + 32 - LocalVarsSize)
#define save_rbx        (rsp + 40 - LocalVarsSize)
#define save_rbp        (rsp + 48 - LocalVarsSize)
#define save_r12        (rsp + 56 - LocalVarsSize)
#define save_r13        (rsp + 64 - LocalVarsSize)
#define save_r14        (rsp + 72 - LocalVarsSize)
#define save_r15        (rsp + 80 - LocalVarsSize)


/*
;  all the +4 offsets are due to the addition of pending_buf_size (in zlib
;  in the deflate_state structure since the asm code was first written
;  (if you compile with zlib 1.0.4 or older, remove the +4).
;  Note : these value are good with a 8 bytes boundary pack structure
*/

#define    MAX_MATCH              258
#define    MIN_MATCH              3
#define    MIN_LOOKAHEAD          (MAX_MATCH+MIN_MATCH+1)

/*
;;; Offsets for fields in the deflate_state structure. These numbers
;;; are calculated from the definition of deflate_state, with the
;;; assumption that the compiler will dword-align the fields. (Thus,
;;; changing the definition of deflate_state could easily cause this
;;; program to crash horribly, without so much as a warning at
;;; compile time. Sigh.)

;  all the +zlib1222add offsets are due to the addition of fields
;  in zlib in the deflate_state structure since the asm code was first written
;  (if you compile with zlib 1.0.4 or older, use "zlib1222add equ (-4)").
;  (if you compile with zlib between 1.0.5 and 1.2.2.1, use "zlib1222add equ 0").
;  if you compile with zlib 1.2.2.2 or later , use "zlib1222add equ 8").
*/



/* you can check the structure offset by running

#include <stdlib.h>
#include <stdio.h>
#include "deflate.h"

void print_depl()
{
deflate_state ds;
deflate_state *s=&ds;
printf("size pointer=%u\n",(int)sizeof(void*));

printf("#define dsWSize         %u\n",(int)(((char*)&(s->w_size))-((char*)s)));
printf("#define dsWMask         %u\n",(int)(((char*)&(s->w_mask))-((char*)s)));
printf("#define dsWindow        %u\n",(int)(((char*)&(s->window))-((char*)s)));
printf("#define dsPrev          %u\n",(int)(((char*)&(s->prev))-((char*)s)));
printf("#define dsMatchLen      %u\n",(int)(((char*)&(s->match_length))-((char*)s)));
printf("#define dsPrevMatch     %u\n",(int)(((char*)&(s->prev_match))-((char*)s)));
printf("#define dsStrStart      %u\n",(int)(((char*)&(s->strstart))-((char*)s)));
printf("#define dsMatchStart    %u\n",(int)(((char*)&(s->match_start))-((char*)s)));
printf("#define dsLookahead     %u\n",(int)(((char*)&(s->lookahead))-((char*)s)));
printf("#define dsPrevLen       %u\n",(int)(((char*)&(s->prev_length))-((char*)s)));
printf("#define dsMaxChainLen   %u\n",(int)(((char*)&(s->max_chain_length))-((char*)s)));
printf("#define dsGoodMatch     %u\n",(int)(((char*)&(s->good_match))-((char*)s)));
printf("#define dsNiceMatch     %u\n",(int)(((char*)&(s->nice_match))-((char*)s)));
}
*/

#define dsWSize          68
#define dsWMask          76
#define dsWindow         80
#define dsPrev           96
#define dsMatchLen       144
#define dsPrevMatch      148
#define dsStrStart       156
#define dsMatchStart     160
#define dsLookahead      164
#define dsPrevLen        168
#define dsMaxChainLen    172
#define dsGoodMatch      188
#define dsNiceMatch      192

#define window_size      [ rcx + dsWSize]
#define WMask            [ rcx + dsWMask]
#define window_ad        [ rcx + dsWindow]
#define prev_ad          [ rcx + dsPrev]
#define strstart         [ rcx + dsStrStart]
#define match_start      [ rcx + dsMatchStart]
#define Lookahead        [ rcx + dsLookahead] //; 0ffffffffh on infozip
#define prev_length      [ rcx + dsPrevLen]
#define max_chain_length [ rcx + dsMaxChainLen]
#define good_match       [ rcx + dsGoodMatch]
#define nice_match       [ rcx + dsNiceMatch]

/*
; windows:
; parameter 1 in rcx(deflate state s), param 2 in rdx (cur match)

; see http://weblogs.asp.net/oldnewthing/archive/2004/01/14/58579.aspx and
; http://msdn.microsoft.com/library/en-us/kmarch/hh/kmarch/64bitAMD_8e951dd2-ee77-4728-8702-55ce4b5dd24a.xml.asp
;
; All registers must be preserved across the call, except for
;   rax, rcx, rdx, r8, r9, r10, and r11, which are scratch.

;
; gcc on macosx-linux:
; see http://www.x86-64.org/documentation/abi-0.99.pdf
; param 1 in rdi, param 2 in rsi
; rbx, rsp, rbp, r12 to r15 must be preserved

;;; Save registers that the compiler may be using, and adjust esp to
;;; make room for our stack frame.


;;; Retrieve the function arguments. r8d will hold cur_match
;;; throughout the entire function. edx will hold the pointer to the
;;; deflate_state structure during the function's setup (before
;;; entering the main loop.

; ms: parameter 1 in rcx (deflate_state* s), param 2 in edx -> r8 (cur match)
; mac: param 1 in rdi, param 2 rsi
; this clear high 32 bits of r8, which can be garbage in both r8 and rdx
*/
        mov [save_rbx],rbx
        mov [save_rbp],rbp


        mov rcx,rdi

        mov r8d,esi


        mov [save_r12],r12
        mov [save_r13],r13
        mov [save_r14],r14
        mov [save_r15],r15


//;;; uInt wmask = s->w_mask;
//;;; unsigned chain_length = s->max_chain_length;
//;;; if (s->prev_length >= s->good_match) {
//;;;     chain_length >>= 2;
//;;; }


        mov edi, prev_length
        mov esi, good_match
        mov eax, WMask
        mov ebx, max_chain_length
        cmp edi, esi
        jl  LastMatchGood
        shr ebx, 2
LastMatchGood:

//;;; chainlen is decremented once beforehand so that the function can
//;;; use the sign flag instead of the zero flag for the exit test.
//;;; It is then shifted into the high word, to make room for the wmask
//;;; value, which it will always accompany.

        dec ebx
        shl ebx, 16
        or  ebx, eax

//;;; on zlib only
//;;; if ((uInt)nice_match > s->lookahead) nice_match = s->lookahead;



        mov eax, nice_match
        mov [chainlenwmask], ebx
        mov r10d, Lookahead
        cmp r10d, eax
        cmovnl r10d, eax
        mov [nicematch],r10d



//;;; register Bytef *scan = s->window + s->strstart;
        mov r10, window_ad
        mov ebp, strstart
        lea r13, [r10 + rbp]

//;;; Determine how many bytes the scan ptr is off from being
//;;; dword-aligned.

         mov r9,r13
         neg r13
         and r13,3

//;;; IPos limit = s->strstart > (IPos)MAX_DIST(s) ?
//;;;     s->strstart - (IPos)MAX_DIST(s) : NIL;


        mov eax, window_size
        sub eax, MIN_LOOKAHEAD


        xor edi,edi
        sub ebp, eax

        mov r11d, prev_length

        cmovng ebp,edi

//;;; int best_len = s->prev_length;


//;;; Store the sum of s->window + best_len in esi locally, and in esi.

       lea  rsi,[r10+r11]

//;;; register ush scan_start = *(ushf*)scan;
//;;; register ush scan_end   = *(ushf*)(scan+best_len-1);
//;;; Posf *prev = s->prev;

        movzx r12d,word ptr [r9]
        movzx ebx, word ptr [r9 + r11 - 1]

        mov rdi, prev_ad

//;;; Jump into the main loop.

        mov edx, [chainlenwmask]

        cmp bx,word ptr [rsi + r8 - 1]
        jz  LookupLoopIsZero
				
						
						
LookupLoop1:
        and r8d, edx

        movzx   r8d, word ptr [rdi + r8*2]
        cmp r8d, ebp
        jbe LeaveNow
		
		
		
        sub edx, 0x00010000
		BEFORE_JMP
        js  LeaveNow
		AFTER_JMP

LoopEntry1:
        cmp bx,word ptr [rsi + r8 - 1]
		BEFORE_JMP
        jz  LookupLoopIsZero
		AFTER_JMP

LookupLoop2:
        and r8d, edx

        movzx   r8d, word ptr [rdi + r8*2]
        cmp r8d, ebp
		BEFORE_JMP
        jbe LeaveNow
		AFTER_JMP
        sub edx, 0x00010000
		BEFORE_JMP
        js  LeaveNow
		AFTER_JMP

LoopEntry2:
        cmp bx,word ptr [rsi + r8 - 1]
		BEFORE_JMP
        jz  LookupLoopIsZero
		AFTER_JMP

LookupLoop4:
        and r8d, edx

        movzx   r8d, word ptr [rdi + r8*2]
        cmp r8d, ebp
		BEFORE_JMP
        jbe LeaveNow
		AFTER_JMP
        sub edx, 0x00010000
		BEFORE_JMP
        js  LeaveNow
		AFTER_JMP

LoopEntry4:

        cmp bx,word ptr [rsi + r8 - 1]
		BEFORE_JMP
        jnz LookupLoop1
        jmp LookupLoopIsZero
		AFTER_JMP
/*
;;; do {
;;;     match = s->window + cur_match;
;;;     if (*(ushf*)(match+best_len-1) != scan_end ||
;;;         *(ushf*)match != scan_start) continue;
;;;     [...]
;;; } while ((cur_match = prev[cur_match & wmask]) > limit
;;;          && --chain_length != 0);
;;;
;;; Here is the inner loop of the function. The function will spend the
;;; majority of its time in this loop, and majority of that time will
;;; be spent in the first ten instructions.
;;;
;;; Within this loop:
;;; ebx = scanend
;;; r8d = curmatch
;;; edx = chainlenwmask - i.e., ((chainlen << 16) | wmask)
;;; esi = windowbestlen - i.e., (window + bestlen)
;;; edi = prev
;;; ebp = limit
*/
.balign 16
LookupLoop:
        and r8d, edx

        movzx   r8d, word ptr [rdi + r8*2]
        cmp r8d, ebp
		BEFORE_JMP
        jbe LeaveNow
		AFTER_JMP
        sub edx, 0x00010000
		BEFORE_JMP
        js  LeaveNow
		AFTER_JMP

LoopEntry:

        cmp bx,word ptr [rsi + r8 - 1]
		BEFORE_JMP
        jnz LookupLoop1
		AFTER_JMP
LookupLoopIsZero:
        cmp     r12w, word ptr [r10 + r8]
		BEFORE_JMP
        jnz LookupLoop1
		AFTER_JMP


//;;; Store the current value of chainlen.
        mov [chainlenwmask], edx
/*
;;; Point edi to the string under scrutiny, and esi to the string we
;;; are hoping to match it up with. In actuality, esi and edi are
;;; both pointed (MAX_MATCH_8 - scanalign) bytes ahead, and edx is
;;; initialized to -(MAX_MATCH_8 - scanalign).
*/
        lea rsi,[r8+r10]
        mov rdx, 0xfffffffffffffef8 //; -(MAX_MATCH_8)
        lea rsi, [rsi + r13 + 0x0108] //;MAX_MATCH_8]
        lea rdi, [r9 + r13 + 0x0108] //;MAX_MATCH_8]

        prefetcht1 [rsi+rdx]
        prefetcht1 [rdi+rdx]

/*
;;; Test the strings for equality, 8 bytes at a time. At the end,
;;; adjust rdx so that it is offset to the exact byte that mismatched.
;;;
;;; We already know at this point that the first three bytes of the
;;; strings match each other, and they can be safely passed over before
;;; starting the compare loop. So what this code does is skip over 0-3
;;; bytes, as much as necessary in order to dword-align the edi
;;; pointer. (rsi will still be misaligned three times out of four.)
;;;
;;; It should be confessed that this loop usually does not represent
;;; much of the total running time. Replacing it with a more
;;; straightforward "rep cmpsb" would not drastically degrade
;;; performance.
*/

LoopCmps:
        mov rax, [rsi + rdx]
        xor rax, [rdi + rdx]
        jnz LeaveLoopCmps

        mov rax, [rsi + rdx + 8]
        xor rax, [rdi + rdx + 8]
        jnz LeaveLoopCmps8


        mov rax, [rsi + rdx + 8+8]
        xor rax, [rdi + rdx + 8+8]
        jnz LeaveLoopCmps16

        add rdx,8+8+8

		BEFORE_JMP
        jnz  LoopCmps
        jmp  LenMaximum
		AFTER_JMP
		
LeaveLoopCmps16: add rdx,8
LeaveLoopCmps8: add rdx,8
LeaveLoopCmps:

        test    eax, 0x0000FFFF
        jnz LenLower

        test eax,0xffffffff

        jnz LenLower32

        add rdx,4
        shr rax,32
        or ax,ax
		BEFORE_JMP
        jnz LenLower
		AFTER_JMP

LenLower32:
        shr eax,16
        add rdx,2
		
LenLower:		
        sub al, 1
        adc rdx, 0
//;;; Calculate the length of the match. If it is longer than MAX_MATCH,
//;;; then automatically accept it as the best possible match and leave.

        lea rax, [rdi + rdx]
        sub rax, r9
        cmp eax, MAX_MATCH
		BEFORE_JMP
        jge LenMaximum
		AFTER_JMP
/*
;;; If the length of the match is not longer than the best match we
;;; have so far, then forget it and return to the lookup loop.
;///////////////////////////////////
*/
        cmp eax, r11d
        jg  LongerMatch

        lea rsi,[r10+r11]

        mov rdi, prev_ad
        mov edx, [chainlenwmask]
		BEFORE_JMP
        jmp LookupLoop
		AFTER_JMP
/*
;;;         s->match_start = cur_match;
;;;         best_len = len;
;;;         if (len >= nice_match) break;
;;;         scan_end = *(ushf*)(scan+best_len-1);
*/
LongerMatch:
        mov r11d, eax
        mov match_start, r8d
        cmp eax, [nicematch]
		BEFORE_JMP
        jge LeaveNow
		AFTER_JMP

        lea rsi,[r10+rax]

        movzx   ebx, word ptr [r9 + rax - 1]
        mov rdi, prev_ad
        mov edx, [chainlenwmask]
		BEFORE_JMP
        jmp LookupLoop
		AFTER_JMP

//;;; Accept the current string, with the maximum possible length.

LenMaximum:
        mov r11d,MAX_MATCH
        mov match_start, r8d

//;;; if ((uInt)best_len <= s->lookahead) return (uInt)best_len;
//;;; return s->lookahead;

LeaveNow:
        mov eax, Lookahead
        cmp r11d, eax
        cmovng eax, r11d



//;;; Restore the stack and return from whence we came.


//        mov rsi,[save_rsi]
//        mov rdi,[save_rdi]
        mov rbx,[save_rbx]
        mov rbp,[save_rbp]
        mov r12,[save_r12]
        mov r13,[save_r13]
        mov r14,[save_r14]
        mov r15,[save_r15]


        ret 0
//; please don't remove this string !
//; Your can freely use gvmat64 in any free or commercial app
//; but it is far better don't remove the string in the binary!
 //   db     0dh,0ah,"asm686 with masm, optimised assembly code from Brian Raiter, written 1998, converted to amd 64 by Gilles Vollant 2005",0dh,0ah,0


match_init:
  ret 0


