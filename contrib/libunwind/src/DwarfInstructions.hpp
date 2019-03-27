//===-------------------------- DwarfInstructions.hpp ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
//  Processor specific interpretation of DWARF unwind info.
//
//===----------------------------------------------------------------------===//

#ifndef __DWARF_INSTRUCTIONS_HPP__
#define __DWARF_INSTRUCTIONS_HPP__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "dwarf2.h"
#include "Registers.hpp"
#include "DwarfParser.hpp"
#include "config.h"


namespace libunwind {


/// DwarfInstructions maps abtract DWARF unwind instructions to a particular
/// architecture
template <typename A, typename R>
class DwarfInstructions {
public:
  typedef typename A::pint_t pint_t;
  typedef typename A::sint_t sint_t;

  static int stepWithDwarf(A &addressSpace, pint_t pc, pint_t fdeStart,
                           R &registers);

private:

  enum {
    DW_X86_64_RET_ADDR = 16
  };

  enum {
    DW_X86_RET_ADDR = 8
  };

  typedef typename CFI_Parser<A>::RegisterLocation  RegisterLocation;
  typedef typename CFI_Parser<A>::PrologInfo        PrologInfo;
  typedef typename CFI_Parser<A>::FDE_Info          FDE_Info;
  typedef typename CFI_Parser<A>::CIE_Info          CIE_Info;

  static pint_t evaluateExpression(pint_t expression, A &addressSpace,
                                   const R &registers,
                                   pint_t initialStackValue);
  static pint_t getSavedRegister(A &addressSpace, const R &registers,
                                 pint_t cfa, const RegisterLocation &savedReg);
  static double getSavedFloatRegister(A &addressSpace, const R &registers,
                                  pint_t cfa, const RegisterLocation &savedReg);
  static v128 getSavedVectorRegister(A &addressSpace, const R &registers,
                                  pint_t cfa, const RegisterLocation &savedReg);

  static pint_t getCFA(A &addressSpace, const PrologInfo &prolog,
                       const R &registers) {
    if (prolog.cfaRegister != 0)
      return (pint_t)((sint_t)registers.getRegister((int)prolog.cfaRegister) +
             prolog.cfaRegisterOffset);
    if (prolog.cfaExpression != 0)
      return evaluateExpression((pint_t)prolog.cfaExpression, addressSpace, 
                                registers, 0);
    assert(0 && "getCFA(): unknown location");
    __builtin_unreachable();
  }
};


template <typename A, typename R>
typename A::pint_t DwarfInstructions<A, R>::getSavedRegister(
    A &addressSpace, const R &registers, pint_t cfa,
    const RegisterLocation &savedReg) {
  switch (savedReg.location) {
  case CFI_Parser<A>::kRegisterInCFA:
    return addressSpace.getRegister(cfa + (pint_t)savedReg.value);

  case CFI_Parser<A>::kRegisterAtExpression:
    return addressSpace.getRegister(
        evaluateExpression((pint_t)savedReg.value, addressSpace,
                            registers, cfa));

  case CFI_Parser<A>::kRegisterIsExpression:
    return evaluateExpression((pint_t)savedReg.value, addressSpace,
                              registers, cfa);

  case CFI_Parser<A>::kRegisterInRegister:
    return registers.getRegister((int)savedReg.value);

  case CFI_Parser<A>::kRegisterUnused:
  case CFI_Parser<A>::kRegisterOffsetFromCFA:
    // FIX ME
    break;
  }
  _LIBUNWIND_ABORT("unsupported restore location for register");
}

template <typename A, typename R>
double DwarfInstructions<A, R>::getSavedFloatRegister(
    A &addressSpace, const R &registers, pint_t cfa,
    const RegisterLocation &savedReg) {
  switch (savedReg.location) {
  case CFI_Parser<A>::kRegisterInCFA:
    return addressSpace.getDouble(cfa + (pint_t)savedReg.value);

  case CFI_Parser<A>::kRegisterAtExpression:
    return addressSpace.getDouble(
        evaluateExpression((pint_t)savedReg.value, addressSpace,
                            registers, cfa));

  case CFI_Parser<A>::kRegisterIsExpression:
  case CFI_Parser<A>::kRegisterUnused:
  case CFI_Parser<A>::kRegisterOffsetFromCFA:
  case CFI_Parser<A>::kRegisterInRegister:
    // FIX ME
    break;
  }
  _LIBUNWIND_ABORT("unsupported restore location for float register");
}

template <typename A, typename R>
v128 DwarfInstructions<A, R>::getSavedVectorRegister(
    A &addressSpace, const R &registers, pint_t cfa,
    const RegisterLocation &savedReg) {
  switch (savedReg.location) {
  case CFI_Parser<A>::kRegisterInCFA:
    return addressSpace.getVector(cfa + (pint_t)savedReg.value);

  case CFI_Parser<A>::kRegisterAtExpression:
    return addressSpace.getVector(
        evaluateExpression((pint_t)savedReg.value, addressSpace,
                            registers, cfa));

  case CFI_Parser<A>::kRegisterIsExpression:
  case CFI_Parser<A>::kRegisterUnused:
  case CFI_Parser<A>::kRegisterOffsetFromCFA:
  case CFI_Parser<A>::kRegisterInRegister:
    // FIX ME
    break;
  }
  _LIBUNWIND_ABORT("unsupported restore location for vector register");
}

template <typename A, typename R>
int DwarfInstructions<A, R>::stepWithDwarf(A &addressSpace, pint_t pc,
                                           pint_t fdeStart, R &registers) {
  FDE_Info fdeInfo;
  CIE_Info cieInfo;
  if (CFI_Parser<A>::decodeFDE(addressSpace, fdeStart, &fdeInfo,
                               &cieInfo) == NULL) {
    PrologInfo prolog;
    if (CFI_Parser<A>::parseFDEInstructions(addressSpace, fdeInfo, cieInfo, pc,
                                            R::getArch(), &prolog)) {
      // get pointer to cfa (architecture specific)
      pint_t cfa = getCFA(addressSpace, prolog, registers);

       // restore registers that DWARF says were saved
      R newRegisters = registers;
      pint_t returnAddress = 0;
      const int lastReg = R::lastDwarfRegNum();
      assert(static_cast<int>(CFI_Parser<A>::kMaxRegisterNumber) >= lastReg &&
             "register range too large");
      assert(lastReg >= (int)cieInfo.returnAddressRegister &&
             "register range does not contain return address register");
      for (int i = 0; i <= lastReg; ++i) {
        if (prolog.savedRegisters[i].location !=
            CFI_Parser<A>::kRegisterUnused) {
          if (registers.validFloatRegister(i))
            newRegisters.setFloatRegister(
                i, getSavedFloatRegister(addressSpace, registers, cfa,
                                         prolog.savedRegisters[i]));
          else if (registers.validVectorRegister(i))
            newRegisters.setVectorRegister(
                i, getSavedVectorRegister(addressSpace, registers, cfa,
                                          prolog.savedRegisters[i]));
          else if (i == (int)cieInfo.returnAddressRegister)
            returnAddress = getSavedRegister(addressSpace, registers, cfa,
                                             prolog.savedRegisters[i]);
          else if (registers.validRegister(i))
            newRegisters.setRegister(
                i, getSavedRegister(addressSpace, registers, cfa,
                                    prolog.savedRegisters[i]));
          else
            return UNW_EBADREG;
        }
      }

      // By definition, the CFA is the stack pointer at the call site, so
      // restoring SP means setting it to CFA.
      newRegisters.setSP(cfa);

#if defined(_LIBUNWIND_TARGET_AARCH64)
      // If the target is aarch64 then the return address may have been signed
      // using the v8.3 pointer authentication extensions. The original
      // return address needs to be authenticated before the return address is
      // restored. autia1716 is used instead of autia as autia1716 assembles
      // to a NOP on pre-v8.3a architectures.
      if ((R::getArch() == REGISTERS_ARM64) &&
          prolog.savedRegisters[UNW_ARM64_RA_SIGN_STATE].value) {
#if !defined(_LIBUNWIND_IS_NATIVE_ONLY)
        return UNW_ECROSSRASIGNING;
#else
        register unsigned long long x17 __asm("x17") = returnAddress;
        register unsigned long long x16 __asm("x16") = cfa;

        // These are the autia1716/autib1716 instructions. The hint instructions
        // are used here as gcc does not assemble autia1716/autib1716 for pre
        // armv8.3a targets.
        if (cieInfo.addressesSignedWithBKey)
          asm("hint 0xe" : "+r"(x17) : "r"(x16)); // autib1716
        else
          asm("hint 0xc" : "+r"(x17) : "r"(x16)); // autia1716
        returnAddress = x17;
#endif
      }
#endif

#if defined(_LIBUNWIND_TARGET_SPARC)
      if (R::getArch() == REGISTERS_SPARC) {
        // Skip call site instruction and delay slot
        returnAddress += 8;
        // Skip unimp instruction if function returns a struct
        if ((addressSpace.get32(returnAddress) & 0xC1C00000) == 0)
          returnAddress += 4;
      }
#endif

      // Return address is address after call site instruction, so setting IP to
      // that does simualates a return.
      newRegisters.setIP(returnAddress);

      // Simulate the step by replacing the register set with the new ones.
      registers = newRegisters;

      return UNW_STEP_SUCCESS;
    }
  }
  return UNW_EBADFRAME;
}

template <typename A, typename R>
typename A::pint_t
DwarfInstructions<A, R>::evaluateExpression(pint_t expression, A &addressSpace,
                                            const R &registers,
                                            pint_t initialStackValue) {
  const bool log = false;
  pint_t p = expression;
  pint_t expressionEnd = expression + 20; // temp, until len read
  pint_t length = (pint_t)addressSpace.getULEB128(p, expressionEnd);
  expressionEnd = p + length;
  if (log)
    fprintf(stderr, "evaluateExpression(): length=%" PRIu64 "\n",
            (uint64_t)length);
  pint_t stack[100];
  pint_t *sp = stack;
  *(++sp) = initialStackValue;

  while (p < expressionEnd) {
    if (log) {
      for (pint_t *t = sp; t > stack; --t) {
        fprintf(stderr, "sp[] = 0x%" PRIx64 "\n", (uint64_t)(*t));
      }
    }
    uint8_t opcode = addressSpace.get8(p++);
    sint_t svalue, svalue2;
    pint_t value;
    uint32_t reg;
    switch (opcode) {
    case DW_OP_addr:
      // push immediate address sized value
      value = addressSpace.getP(p);
      p += sizeof(pint_t);
      *(++sp) = value;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_deref:
      // pop stack, dereference, push result
      value = *sp--;
      *(++sp) = addressSpace.getP(value);
      if (log)
        fprintf(stderr, "dereference 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_const1u:
      // push immediate 1 byte value
      value = addressSpace.get8(p);
      p += 1;
      *(++sp) = value;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_const1s:
      // push immediate 1 byte signed value
      svalue = (int8_t) addressSpace.get8(p);
      p += 1;
      *(++sp) = (pint_t)svalue;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)svalue);
      break;

    case DW_OP_const2u:
      // push immediate 2 byte value
      value = addressSpace.get16(p);
      p += 2;
      *(++sp) = value;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_const2s:
      // push immediate 2 byte signed value
      svalue = (int16_t) addressSpace.get16(p);
      p += 2;
      *(++sp) = (pint_t)svalue;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)svalue);
      break;

    case DW_OP_const4u:
      // push immediate 4 byte value
      value = addressSpace.get32(p);
      p += 4;
      *(++sp) = value;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_const4s:
      // push immediate 4 byte signed value
      svalue = (int32_t)addressSpace.get32(p);
      p += 4;
      *(++sp) = (pint_t)svalue;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)svalue);
      break;

    case DW_OP_const8u:
      // push immediate 8 byte value
      value = (pint_t)addressSpace.get64(p);
      p += 8;
      *(++sp) = value;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_const8s:
      // push immediate 8 byte signed value
      value = (pint_t)addressSpace.get64(p);
      p += 8;
      *(++sp) = value;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_constu:
      // push immediate ULEB128 value
      value = (pint_t)addressSpace.getULEB128(p, expressionEnd);
      *(++sp) = value;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_consts:
      // push immediate SLEB128 value
      svalue = (sint_t)addressSpace.getSLEB128(p, expressionEnd);
      *(++sp) = (pint_t)svalue;
      if (log)
        fprintf(stderr, "push 0x%" PRIx64 "\n", (uint64_t)svalue);
      break;

    case DW_OP_dup:
      // push top of stack
      value = *sp;
      *(++sp) = value;
      if (log)
        fprintf(stderr, "duplicate top of stack\n");
      break;

    case DW_OP_drop:
      // pop
      --sp;
      if (log)
        fprintf(stderr, "pop top of stack\n");
      break;

    case DW_OP_over:
      // dup second
      value = sp[-1];
      *(++sp) = value;
      if (log)
        fprintf(stderr, "duplicate second in stack\n");
      break;

    case DW_OP_pick:
      // pick from
      reg = addressSpace.get8(p);
      p += 1;
      value = sp[-reg];
      *(++sp) = value;
      if (log)
        fprintf(stderr, "duplicate %d in stack\n", reg);
      break;

    case DW_OP_swap:
      // swap top two
      value = sp[0];
      sp[0] = sp[-1];
      sp[-1] = value;
      if (log)
        fprintf(stderr, "swap top of stack\n");
      break;

    case DW_OP_rot:
      // rotate top three
      value = sp[0];
      sp[0] = sp[-1];
      sp[-1] = sp[-2];
      sp[-2] = value;
      if (log)
        fprintf(stderr, "rotate top three of stack\n");
      break;

    case DW_OP_xderef:
      // pop stack, dereference, push result
      value = *sp--;
      *sp = *((pint_t*)value);
      if (log)
        fprintf(stderr, "x-dereference 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_abs:
      svalue = (sint_t)*sp;
      if (svalue < 0)
        *sp = (pint_t)(-svalue);
      if (log)
        fprintf(stderr, "abs\n");
      break;

    case DW_OP_and:
      value = *sp--;
      *sp &= value;
      if (log)
        fprintf(stderr, "and\n");
      break;

    case DW_OP_div:
      svalue = (sint_t)(*sp--);
      svalue2 = (sint_t)*sp;
      *sp = (pint_t)(svalue2 / svalue);
      if (log)
        fprintf(stderr, "div\n");
      break;

    case DW_OP_minus:
      value = *sp--;
      *sp = *sp - value;
      if (log)
        fprintf(stderr, "minus\n");
      break;

    case DW_OP_mod:
      svalue = (sint_t)(*sp--);
      svalue2 = (sint_t)*sp;
      *sp = (pint_t)(svalue2 % svalue);
      if (log)
        fprintf(stderr, "module\n");
      break;

    case DW_OP_mul:
      svalue = (sint_t)(*sp--);
      svalue2 = (sint_t)*sp;
      *sp = (pint_t)(svalue2 * svalue);
      if (log)
        fprintf(stderr, "mul\n");
      break;

    case DW_OP_neg:
      *sp = 0 - *sp;
      if (log)
        fprintf(stderr, "neg\n");
      break;

    case DW_OP_not:
      svalue = (sint_t)(*sp);
      *sp = (pint_t)(~svalue);
      if (log)
        fprintf(stderr, "not\n");
      break;

    case DW_OP_or:
      value = *sp--;
      *sp |= value;
      if (log)
        fprintf(stderr, "or\n");
      break;

    case DW_OP_plus:
      value = *sp--;
      *sp += value;
      if (log)
        fprintf(stderr, "plus\n");
      break;

    case DW_OP_plus_uconst:
      // pop stack, add uelb128 constant, push result
      *sp += static_cast<pint_t>(addressSpace.getULEB128(p, expressionEnd));
      if (log)
        fprintf(stderr, "add constant\n");
      break;

    case DW_OP_shl:
      value = *sp--;
      *sp = *sp << value;
      if (log)
        fprintf(stderr, "shift left\n");
      break;

    case DW_OP_shr:
      value = *sp--;
      *sp = *sp >> value;
      if (log)
        fprintf(stderr, "shift left\n");
      break;

    case DW_OP_shra:
      value = *sp--;
      svalue = (sint_t)*sp;
      *sp = (pint_t)(svalue >> value);
      if (log)
        fprintf(stderr, "shift left arithmetric\n");
      break;

    case DW_OP_xor:
      value = *sp--;
      *sp ^= value;
      if (log)
        fprintf(stderr, "xor\n");
      break;

    case DW_OP_skip:
      svalue = (int16_t) addressSpace.get16(p);
      p += 2;
      p = (pint_t)((sint_t)p + svalue);
      if (log)
        fprintf(stderr, "skip %" PRIu64 "\n", (uint64_t)svalue);
      break;

    case DW_OP_bra:
      svalue = (int16_t) addressSpace.get16(p);
      p += 2;
      if (*sp--)
        p = (pint_t)((sint_t)p + svalue);
      if (log)
        fprintf(stderr, "bra %" PRIu64 "\n", (uint64_t)svalue);
      break;

    case DW_OP_eq:
      value = *sp--;
      *sp = (*sp == value);
      if (log)
        fprintf(stderr, "eq\n");
      break;

    case DW_OP_ge:
      value = *sp--;
      *sp = (*sp >= value);
      if (log)
        fprintf(stderr, "ge\n");
      break;

    case DW_OP_gt:
      value = *sp--;
      *sp = (*sp > value);
      if (log)
        fprintf(stderr, "gt\n");
      break;

    case DW_OP_le:
      value = *sp--;
      *sp = (*sp <= value);
      if (log)
        fprintf(stderr, "le\n");
      break;

    case DW_OP_lt:
      value = *sp--;
      *sp = (*sp < value);
      if (log)
        fprintf(stderr, "lt\n");
      break;

    case DW_OP_ne:
      value = *sp--;
      *sp = (*sp != value);
      if (log)
        fprintf(stderr, "ne\n");
      break;

    case DW_OP_lit0:
    case DW_OP_lit1:
    case DW_OP_lit2:
    case DW_OP_lit3:
    case DW_OP_lit4:
    case DW_OP_lit5:
    case DW_OP_lit6:
    case DW_OP_lit7:
    case DW_OP_lit8:
    case DW_OP_lit9:
    case DW_OP_lit10:
    case DW_OP_lit11:
    case DW_OP_lit12:
    case DW_OP_lit13:
    case DW_OP_lit14:
    case DW_OP_lit15:
    case DW_OP_lit16:
    case DW_OP_lit17:
    case DW_OP_lit18:
    case DW_OP_lit19:
    case DW_OP_lit20:
    case DW_OP_lit21:
    case DW_OP_lit22:
    case DW_OP_lit23:
    case DW_OP_lit24:
    case DW_OP_lit25:
    case DW_OP_lit26:
    case DW_OP_lit27:
    case DW_OP_lit28:
    case DW_OP_lit29:
    case DW_OP_lit30:
    case DW_OP_lit31:
      value = static_cast<pint_t>(opcode - DW_OP_lit0);
      *(++sp) = value;
      if (log)
        fprintf(stderr, "push literal 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_reg0:
    case DW_OP_reg1:
    case DW_OP_reg2:
    case DW_OP_reg3:
    case DW_OP_reg4:
    case DW_OP_reg5:
    case DW_OP_reg6:
    case DW_OP_reg7:
    case DW_OP_reg8:
    case DW_OP_reg9:
    case DW_OP_reg10:
    case DW_OP_reg11:
    case DW_OP_reg12:
    case DW_OP_reg13:
    case DW_OP_reg14:
    case DW_OP_reg15:
    case DW_OP_reg16:
    case DW_OP_reg17:
    case DW_OP_reg18:
    case DW_OP_reg19:
    case DW_OP_reg20:
    case DW_OP_reg21:
    case DW_OP_reg22:
    case DW_OP_reg23:
    case DW_OP_reg24:
    case DW_OP_reg25:
    case DW_OP_reg26:
    case DW_OP_reg27:
    case DW_OP_reg28:
    case DW_OP_reg29:
    case DW_OP_reg30:
    case DW_OP_reg31:
      reg = static_cast<uint32_t>(opcode - DW_OP_reg0);
      *(++sp) = registers.getRegister((int)reg);
      if (log)
        fprintf(stderr, "push reg %d\n", reg);
      break;

    case DW_OP_regx:
      reg = static_cast<uint32_t>(addressSpace.getULEB128(p, expressionEnd));
      *(++sp) = registers.getRegister((int)reg);
      if (log)
        fprintf(stderr, "push reg %d + 0x%" PRIx64 "\n", reg, (uint64_t)svalue);
      break;

    case DW_OP_breg0:
    case DW_OP_breg1:
    case DW_OP_breg2:
    case DW_OP_breg3:
    case DW_OP_breg4:
    case DW_OP_breg5:
    case DW_OP_breg6:
    case DW_OP_breg7:
    case DW_OP_breg8:
    case DW_OP_breg9:
    case DW_OP_breg10:
    case DW_OP_breg11:
    case DW_OP_breg12:
    case DW_OP_breg13:
    case DW_OP_breg14:
    case DW_OP_breg15:
    case DW_OP_breg16:
    case DW_OP_breg17:
    case DW_OP_breg18:
    case DW_OP_breg19:
    case DW_OP_breg20:
    case DW_OP_breg21:
    case DW_OP_breg22:
    case DW_OP_breg23:
    case DW_OP_breg24:
    case DW_OP_breg25:
    case DW_OP_breg26:
    case DW_OP_breg27:
    case DW_OP_breg28:
    case DW_OP_breg29:
    case DW_OP_breg30:
    case DW_OP_breg31:
      reg = static_cast<uint32_t>(opcode - DW_OP_breg0);
      svalue = (sint_t)addressSpace.getSLEB128(p, expressionEnd);
      svalue += static_cast<sint_t>(registers.getRegister((int)reg));
      *(++sp) = (pint_t)(svalue);
      if (log)
        fprintf(stderr, "push reg %d + 0x%" PRIx64 "\n", reg, (uint64_t)svalue);
      break;

    case DW_OP_bregx:
      reg = static_cast<uint32_t>(addressSpace.getULEB128(p, expressionEnd));
      svalue = (sint_t)addressSpace.getSLEB128(p, expressionEnd);
      svalue += static_cast<sint_t>(registers.getRegister((int)reg));
      *(++sp) = (pint_t)(svalue);
      if (log)
        fprintf(stderr, "push reg %d + 0x%" PRIx64 "\n", reg, (uint64_t)svalue);
      break;

    case DW_OP_fbreg:
      _LIBUNWIND_ABORT("DW_OP_fbreg not implemented");
      break;

    case DW_OP_piece:
      _LIBUNWIND_ABORT("DW_OP_piece not implemented");
      break;

    case DW_OP_deref_size:
      // pop stack, dereference, push result
      value = *sp--;
      switch (addressSpace.get8(p++)) {
      case 1:
        value = addressSpace.get8(value);
        break;
      case 2:
        value = addressSpace.get16(value);
        break;
      case 4:
        value = addressSpace.get32(value);
        break;
      case 8:
        value = (pint_t)addressSpace.get64(value);
        break;
      default:
        _LIBUNWIND_ABORT("DW_OP_deref_size with bad size");
      }
      *(++sp) = value;
      if (log)
        fprintf(stderr, "sized dereference 0x%" PRIx64 "\n", (uint64_t)value);
      break;

    case DW_OP_xderef_size:
    case DW_OP_nop:
    case DW_OP_push_object_addres:
    case DW_OP_call2:
    case DW_OP_call4:
    case DW_OP_call_ref:
    default:
      _LIBUNWIND_ABORT("DWARF opcode not implemented");
    }

  }
  if (log)
    fprintf(stderr, "expression evaluates to 0x%" PRIx64 "\n", (uint64_t)*sp);
  return *sp;
}



} // namespace libunwind

#endif // __DWARF_INSTRUCTIONS_HPP__
