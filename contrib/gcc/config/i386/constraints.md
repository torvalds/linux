;; Constraint definitions for IA-32 and x86-64.
;; Copyright (C) 2006 Free Software Foundation, Inc.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;;; Unused letters:
;;;     B     H           TU W   
;;;           h jk          vw  z

;; Integer register constraints.
;; It is not necessary to define 'r' here.
(define_register_constraint "R" "LEGACY_REGS"
 "Legacy register---the eight integer registers available on all
  i386 processors (@code{a}, @code{b}, @code{c}, @code{d},
  @code{si}, @code{di}, @code{bp}, @code{sp}).")

(define_register_constraint "q" "TARGET_64BIT ? GENERAL_REGS : Q_REGS"
 "Any register accessible as @code{@var{r}l}.  In 32-bit mode, @code{a},
  @code{b}, @code{c}, and @code{d}; in 64-bit mode, any integer register.")

(define_register_constraint "Q" "Q_REGS"
 "Any register accessible as @code{@var{r}h}: @code{a}, @code{b},
  @code{c}, and @code{d}.")

(define_register_constraint "l" "INDEX_REGS"
 "@internal Any register that can be used as the index in a base+index
  memory access: that is, any general register except the stack pointer.")

(define_register_constraint "a" "AREG"
 "The @code{a} register.")

(define_register_constraint "b" "BREG"
 "The @code{b} register.")

(define_register_constraint "c" "CREG"
 "The @code{c} register.")

(define_register_constraint "d" "DREG"
 "The @code{d} register.")

(define_register_constraint "S" "SIREG"
 "The @code{si} register.")

(define_register_constraint "D" "DIREG"
 "The @code{di} register.")

(define_register_constraint "A" "AD_REGS"
 "The @code{a} and @code{d} registers, as a pair (for instructions
  that return half the result in one and half in the other).")

;; Floating-point register constraints.
(define_register_constraint "f"
 "TARGET_80387 || TARGET_FLOAT_RETURNS_IN_80387 ? FLOAT_REGS : NO_REGS"
 "Any 80387 floating-point (stack) register.")

(define_register_constraint "t"
 "TARGET_80387 || TARGET_FLOAT_RETURNS_IN_80387 ? FP_TOP_REG : NO_REGS"
 "Top of 80387 floating-point stack (@code{%st(0)}).")

(define_register_constraint "u"
 "TARGET_80387 || TARGET_FLOAT_RETURNS_IN_80387 ? FP_SECOND_REG : NO_REGS"
 "Second from top of 80387 floating-point stack (@code{%st(1)}).")

;; Vector registers (also used for plain floating point nowadays).
(define_register_constraint "y" "TARGET_MMX ? MMX_REGS : NO_REGS"
 "Any MMX register.")

(define_register_constraint "x" "TARGET_SSE ? SSE_REGS : NO_REGS"
 "Any SSE register.")

(define_register_constraint "Y" "TARGET_SSE2? SSE_REGS : NO_REGS"
 "@internal Any SSE2 register.")

;; Integer constant constraints.
(define_constraint "I"
  "Integer constant in the range 0 @dots{} 31, for 32-bit shifts."
  (and (match_code "const_int")
       (match_test "ival >= 0 && ival <= 31")))

(define_constraint "J"
  "Integer constant in the range 0 @dots{} 63, for 64-bit shifts."
  (and (match_code "const_int")
       (match_test "ival >= 0 && ival <= 63")))

(define_constraint "K"
  "Signed 8-bit integer constant."
  (and (match_code "const_int")
       (match_test "ival >= -128 && ival <= 127")))

(define_constraint "L"
  "@code{0xFF} or @code{0xFFFF}, for andsi as a zero-extending move."
  (and (match_code "const_int")
       (match_test "ival == 0xFF || ival == 0xFFFF")))

(define_constraint "M"
  "0, 1, 2, or 3 (shifts for the @code{lea} instruction)."
  (and (match_code "const_int")
       (match_test "ival >= 0 && ival <= 3")))

(define_constraint "N"
  "Unsigned 8-bit integer constant (for @code{in} and @code{out} 
   instructions)."
  (and (match_code "const_int")
       (match_test "ival >= 0 && ival <= 255")))

(define_constraint "O"
  "@internal Integer constant in the range 0 @dots{} 127, for 128-bit shifts."
  (and (match_code "const_int")
       (match_test "ival >= 0 && ival <= 127")))

;; Floating-point constant constraints.
;; We allow constants even if TARGET_80387 isn't set, because the
;; stack register converter may need to load 0.0 into the function
;; value register (top of stack).
(define_constraint "G"
  "Standard 80387 floating point constant."
  (and (match_code "const_double")
       (match_test "standard_80387_constant_p (op)")))

;; This can theoretically be any mode's CONST0_RTX.
(define_constraint "C"
  "Standard SSE floating point constant."
  (match_test "standard_sse_constant_p (op)"))

;; Constant-or-symbol-reference constraints.

(define_constraint "e"
  "32-bit signed integer constant, or a symbolic reference known
   to fit that range (for immediate operands in sign-extending x86-64
   instructions)."
  (match_operand 0 "x86_64_immediate_operand"))

(define_constraint "Z"
  "32-bit unsigned integer constant, or a symbolic reference known
   to fit that range (for immediate operands in zero-extending x86-64
   instructions)."
  (match_operand 0 "x86_64_zext_immediate_operand"))
