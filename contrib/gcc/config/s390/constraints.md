;; Constraints definitions belonging to the gcc backend for IBM S/390.
;; Copyright (C) 2006 Free Software Foundation, Inc.
;; Written by Wolfgang Gellerich, using code and information found in
;; files s390.md, s390.h, and s390.c.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it under
;; the terms of the GNU General Public License as published by the Free
;; Software Foundation; either version 2, or (at your option) any later
;; version.
;;
;; GCC is distributed in the hope that it will be useful, but WITHOUT ANY
;; WARRANTY; without even the implied warranty of MERCHANTABILITY or
;; FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
;; for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301, USA.


;;
;; Special constraints for s/390 machine description:
;;
;;    a -- Any address register from 1 to 15.
;;    c -- Condition code register 33.
;;    d -- Any register from 0 to 15.
;;    f -- Floating point registers.
;;    t -- Access registers 36 and 37.
;;    G -- Const double zero operand
;;    I -- An 8-bit constant (0..255).
;;    J -- A 12-bit constant (0..4095).
;;    K -- A 16-bit constant (-32768..32767).
;;    L -- Value appropriate as displacement.
;;         (0..4095) for short displacement
;;         (-524288..524287) for long displacement
;;    M -- Constant integer with a value of 0x7fffffff.
;;    N -- Multiple letter constraint followed by 4 parameter letters.
;;         0..9,x:  number of the part counting from most to least significant
;;         H,Q:     mode of the part
;;         D,S,H:   mode of the containing operand
;;         0,F:     value of the other parts (F - all bits set)
;;
;;         The constraint matches if the specified part of a constant
;;         has a value different from its other parts.  If the letter x
;;         is specified instead of a part number, the constraint matches
;;         if there is any single part with non-default value.
;;    O -- Multiple letter constraint followed by 1 parameter.
;;         s:  Signed extended immediate value (-2G .. 2G-1).
;;         p:  Positive extended immediate value (0 .. 4G-1).
;;         n:  Negative extended immediate value (-4G .. -1).
;;         These constraints do not accept any operand if the machine does
;;         not provide the extended-immediate facility.
;;    P -- Any integer constant that can be loaded without literal pool.
;;    Q -- Memory reference without index register and with short displacement.
;;    R -- Memory reference with index register and short displacement.
;;    S -- Memory reference without index register but with long displacement.
;;    T -- Memory reference with index register and long displacement.
;;    A -- Multiple letter constraint followed by Q, R, S, or T:
;;         Offsettable memory reference of type specified by second letter.
;;    B -- Multiple letter constraint followed by Q, R, S, or T:
;;         Memory reference of the type specified by second letter that
;;         does *not* refer to a literal pool entry.
;;    U -- Pointer with short displacement.
;;    W -- Pointer with long displacement.
;;    Y -- Shift count operand.
;;


;;
;;  Register constraints.
;;

(define_register_constraint "a" 
  "ADDR_REGS"
  "Any address register from 1 to 15.")


(define_register_constraint "c" 
  "CC_REGS"
  "Condition code register 33")


(define_register_constraint "d" 
  "GENERAL_REGS"
  "Any register from 0 to 15")


(define_register_constraint "f" 
  "FP_REGS"
  "Floating point registers")


(define_register_constraint "t" 
  "ACCESS_REGS"
  "@internal
   Access registers 36 and 37")


;;
;;  General constraints for constants.
;;

(define_constraint "G"
  "@internal
   Const double zero operand"
   (and (match_code "const_double")
        (match_test "s390_float_const_zero_p (op)")))


(define_constraint "I"
  "An 8-bit constant (0..255)"
  (and (match_code "const_int")
       (match_test "(unsigned int) ival <= 255")))


(define_constraint "J"
  "A 12-bit constant (0..4095)"
  (and (match_code "const_int")
       (match_test "(unsigned int) ival <= 4095")))


(define_constraint "K"
  "A 16-bit constant (-32768..32767)"
  (and (match_code "const_int")
       (match_test "ival >= -32768 && ival <= 32767")))



(define_constraint "L"
  "Value appropriate as displacement.
      (0..4095) for short displacement
      (-524288..524287) for long displacement"
  (and (match_code "const_int")
       (match_test "TARGET_LONG_DISPLACEMENT ?
              (ival >= -524288 && ival <= 524287)
            : (ival >= 0 && ival <= 4095)")))


(define_constraint "M"
  "Constant integer with a value of 0x7fffffff"
  (and (match_code "const_int")
       (match_test "ival == 2147483647")))


(define_constraint "P"
  "@internal
   Any integer constant that can be loaded without literal pool"
   (and (match_code "const_int")
        (match_test "legitimate_reload_constant_p (GEN_INT (ival))")))


(define_address_constraint "Y"
  "Shift count operand"

;; Simply check for the basic form of a shift count.  Reload will
;; take care of making sure we have a proper base register.

  (match_test "s390_decompose_shift_count (op, NULL, NULL)"  ))


;;    N -- Multiple letter constraint followed by 4 parameter letters.
;;         0..9,x:  number of the part counting from most to least significant
;;         H,Q:     mode of the part
;;         D,S,H:   mode of the containing operand
;;         0,F:     value of the other parts (F = all bits set)
;;
;;         The constraint matches if the specified part of a constant
;;         has a value different from its other parts.  If the letter x
;;         is specified instead of a part number, the constraint matches
;;         if there is any single part with non-default value.
;;
;; The following patterns define only those constraints that are actually 
;; used in s390.md.  If you need an additional one, simply add it in the 
;; obvious way.  Function s390_N_constraint_str is ready to handle all 
;; combinations.
;;


(define_constraint "NxQS0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"xQS0\", ival)")))


(define_constraint "NxQD0"
  "@internal"
   (and (match_code "const_int")
        (match_test "s390_N_constraint_str (\"xQD0\", ival)")))


(define_constraint "N3HD0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"3HD0\", ival)")))


(define_constraint "N2HD0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"2HD0\", ival)")))


(define_constraint "N1SD0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"1SD0\", ival)")))


(define_constraint "N1HS0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"1HS0\", ival)")))


(define_constraint "N1HD0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"1HD0\", ival)")))


(define_constraint "N0SD0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"0SD0\", ival)")))


(define_constraint "N0HS0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"0HS0\", ival)")))


(define_constraint "N0HD0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"0HD0\", ival)")))


(define_constraint "NxQDF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"xQDF\", ival)")))


(define_constraint "N1SDF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"1SDF\", ival)")))


(define_constraint "N0SDF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"0SDF\", ival)")))


(define_constraint "N3HDF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"3HDF\", ival)")))


(define_constraint "N2HDF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"2HDF\", ival)")))


(define_constraint "N1HDF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"1HDF\", ival)")))


(define_constraint "N0HDF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"0HDF\", ival)")))


(define_constraint "N0HSF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"0HSF\", ival)")))


(define_constraint "N1HSF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"1HSF\", ival)")))


(define_constraint "NxQSF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"xQSF\", ival)")))


(define_constraint "NxQHF"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"xQHF\", ival)")))


(define_constraint "NxQH0"
  "@internal"
  (and (match_code "const_int")
       (match_test "s390_N_constraint_str (\"xQH0\", ival)")))




;;
;; Double-letter constraints starting with O follow.
;;


(define_constraint "Os"
  "@internal
   Signed extended immediate value (-2G .. 2G-1).
   This constraint will only match if the machine provides
   the extended-immediate facility."
  (and (match_code "const_int")
       (match_test "s390_O_constraint_str ('s', ival)")))


(define_constraint "Op"
  "@internal
   Positive extended immediate value (0 .. 4G-1).
   This constraint will only match if the machine provides
   the extended-immediate facility."
  (and (match_code "const_int")
       (match_test "s390_O_constraint_str ('p', ival)")))


(define_constraint "On"
  "@internal
   Negative extended immediate value (-4G .. -1).
   This constraint will only match if the machine provides
   the extended-immediate facility."
  (and (match_code "const_int")
       (match_test "s390_O_constraint_str ('n', ival)")))




;;
;; Memory constraints follow.
;;

(define_memory_constraint "Q"
  "Memory reference without index register and with short displacement"
  (match_test "s390_mem_constraint (\"Q\", op)"))



(define_memory_constraint "R"
  "Memory reference with index register and short displacement"
  (match_test "s390_mem_constraint (\"R\", op)"))


(define_memory_constraint "S"
  "Memory reference without index register but with long displacement"
  (match_test "s390_mem_constraint (\"S\", op)"))


(define_memory_constraint "T"
  "Memory reference with index register and long displacement"
  (match_test "s390_mem_constraint (\"T\", op)"))



(define_memory_constraint "AQ"
  "@internal 
   Offsettable memory reference without index register and with short displacement"
  (match_test "s390_mem_constraint (\"AQ\", op)"))


(define_memory_constraint "AR"
  "@internal 
   Offsettable memory reference with index register and short displacement"
  (match_test "s390_mem_constraint (\"AR\", op)"))


(define_memory_constraint "AS"
  "@internal 
   Offsettable memory reference without index register but with long displacement"
  (match_test "s390_mem_constraint (\"AS\", op)"))


(define_memory_constraint "AT"
  "@internal 
   Offsettable memory reference with index register and long displacement"
  (match_test "s390_mem_constraint (\"AT\", op)"))



(define_constraint "BQ"
  "@internal 
   Memory reference without index register and with short 
   displacement that does *not* refer to a literal pool entry."
  (match_test "s390_mem_constraint (\"BQ\", op)"))


(define_constraint "BR"
  "@internal 
   Memory reference with index register and short displacement that
   does *not* refer to a literal pool entry. "
  (match_test "s390_mem_constraint (\"BR\", op)"))


(define_constraint "BS"
  "@internal 
   Memory reference without index register but with long displacement
   that does *not* refer to a literal pool entry. "
  (match_test "s390_mem_constraint (\"BS\", op)"))


(define_constraint "BT"
  "@internal 
   Memory reference with index register and long displacement that
   does *not* refer to a literal pool entry. "
  (match_test "s390_mem_constraint (\"BT\", op)"))



(define_address_constraint "U"
  "Pointer with short displacement"
  (match_test "s390_mem_constraint (\"U\", op)"))



(define_address_constraint "W"
  "Pointer with long displacement"
  (match_test "s390_mem_constraint (\"W\", op)"))
