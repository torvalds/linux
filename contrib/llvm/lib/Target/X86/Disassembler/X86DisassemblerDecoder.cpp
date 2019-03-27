//===-- X86DisassemblerDecoder.cpp - Disassembler decoder -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler.
// It contains the implementation of the instruction decoder.
// Documentation for the disassembler can be found in X86Disassembler.h.
//
//===----------------------------------------------------------------------===//

#include <cstdarg> /* for va_*()       */
#include <cstdio>  /* for vsnprintf()  */
#include <cstdlib> /* for exit()       */
#include <cstring> /* for memset()     */

#include "X86DisassemblerDecoder.h"

using namespace llvm::X86Disassembler;

/// Specifies whether a ModR/M byte is needed and (if so) which
/// instruction each possible value of the ModR/M byte corresponds to.  Once
/// this information is known, we have narrowed down to a single instruction.
struct ModRMDecision {
  uint8_t modrm_type;
  uint16_t instructionIDs;
};

/// Specifies which set of ModR/M->instruction tables to look at
/// given a particular opcode.
struct OpcodeDecision {
  ModRMDecision modRMDecisions[256];
};

/// Specifies which opcode->instruction tables to look at given
/// a particular context (set of attributes).  Since there are many possible
/// contexts, the decoder first uses CONTEXTS_SYM to determine which context
/// applies given a specific set of attributes.  Hence there are only IC_max
/// entries in this table, rather than 2^(ATTR_max).
struct ContextDecision {
  OpcodeDecision opcodeDecisions[IC_max];
};

#include "X86GenDisassemblerTables.inc"

#ifndef NDEBUG
#define debug(s) do { Debug(__FILE__, __LINE__, s); } while (0)
#else
#define debug(s) do { } while (0)
#endif

/*
 * contextForAttrs - Client for the instruction context table.  Takes a set of
 *   attributes and returns the appropriate decode context.
 *
 * @param attrMask  - Attributes, from the enumeration attributeBits.
 * @return          - The InstructionContext to use when looking up an
 *                    an instruction with these attributes.
 */
static InstructionContext contextForAttrs(uint16_t attrMask) {
  return static_cast<InstructionContext>(CONTEXTS_SYM[attrMask]);
}

/*
 * modRMRequired - Reads the appropriate instruction table to determine whether
 *   the ModR/M byte is required to decode a particular instruction.
 *
 * @param type        - The opcode type (i.e., how many bytes it has).
 * @param insnContext - The context for the instruction, as returned by
 *                      contextForAttrs.
 * @param opcode      - The last byte of the instruction's opcode, not counting
 *                      ModR/M extensions and escapes.
 * @return            - true if the ModR/M byte is required, false otherwise.
 */
static int modRMRequired(OpcodeType type,
                         InstructionContext insnContext,
                         uint16_t opcode) {
  const struct ContextDecision* decision = nullptr;

  switch (type) {
  case ONEBYTE:
    decision = &ONEBYTE_SYM;
    break;
  case TWOBYTE:
    decision = &TWOBYTE_SYM;
    break;
  case THREEBYTE_38:
    decision = &THREEBYTE38_SYM;
    break;
  case THREEBYTE_3A:
    decision = &THREEBYTE3A_SYM;
    break;
  case XOP8_MAP:
    decision = &XOP8_MAP_SYM;
    break;
  case XOP9_MAP:
    decision = &XOP9_MAP_SYM;
    break;
  case XOPA_MAP:
    decision = &XOPA_MAP_SYM;
    break;
  case THREEDNOW_MAP:
    decision = &THREEDNOW_MAP_SYM;
    break;
  }

  return decision->opcodeDecisions[insnContext].modRMDecisions[opcode].
    modrm_type != MODRM_ONEENTRY;
}

/*
 * decode - Reads the appropriate instruction table to obtain the unique ID of
 *   an instruction.
 *
 * @param type        - See modRMRequired().
 * @param insnContext - See modRMRequired().
 * @param opcode      - See modRMRequired().
 * @param modRM       - The ModR/M byte if required, or any value if not.
 * @return            - The UID of the instruction, or 0 on failure.
 */
static InstrUID decode(OpcodeType type,
                       InstructionContext insnContext,
                       uint8_t opcode,
                       uint8_t modRM) {
  const struct ModRMDecision* dec = nullptr;

  switch (type) {
  case ONEBYTE:
    dec = &ONEBYTE_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case TWOBYTE:
    dec = &TWOBYTE_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case THREEBYTE_38:
    dec = &THREEBYTE38_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case THREEBYTE_3A:
    dec = &THREEBYTE3A_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case XOP8_MAP:
    dec = &XOP8_MAP_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case XOP9_MAP:
    dec = &XOP9_MAP_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case XOPA_MAP:
    dec = &XOPA_MAP_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case THREEDNOW_MAP:
    dec = &THREEDNOW_MAP_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  }

  switch (dec->modrm_type) {
  default:
    debug("Corrupt table!  Unknown modrm_type");
    return 0;
  case MODRM_ONEENTRY:
    return modRMTable[dec->instructionIDs];
  case MODRM_SPLITRM:
    if (modFromModRM(modRM) == 0x3)
      return modRMTable[dec->instructionIDs+1];
    return modRMTable[dec->instructionIDs];
  case MODRM_SPLITREG:
    if (modFromModRM(modRM) == 0x3)
      return modRMTable[dec->instructionIDs+((modRM & 0x38) >> 3)+8];
    return modRMTable[dec->instructionIDs+((modRM & 0x38) >> 3)];
  case MODRM_SPLITMISC:
    if (modFromModRM(modRM) == 0x3)
      return modRMTable[dec->instructionIDs+(modRM & 0x3f)+8];
    return modRMTable[dec->instructionIDs+((modRM & 0x38) >> 3)];
  case MODRM_FULL:
    return modRMTable[dec->instructionIDs+modRM];
  }
}

/*
 * specifierForUID - Given a UID, returns the name and operand specification for
 *   that instruction.
 *
 * @param uid - The unique ID for the instruction.  This should be returned by
 *              decode(); specifierForUID will not check bounds.
 * @return    - A pointer to the specification for that instruction.
 */
static const struct InstructionSpecifier *specifierForUID(InstrUID uid) {
  return &INSTRUCTIONS_SYM[uid];
}

/*
 * consumeByte - Uses the reader function provided by the user to consume one
 *   byte from the instruction's memory and advance the cursor.
 *
 * @param insn  - The instruction with the reader function to use.  The cursor
 *                for this instruction is advanced.
 * @param byte  - A pointer to a pre-allocated memory buffer to be populated
 *                with the data read.
 * @return      - 0 if the read was successful; nonzero otherwise.
 */
static int consumeByte(struct InternalInstruction* insn, uint8_t* byte) {
  int ret = insn->reader(insn->readerArg, byte, insn->readerCursor);

  if (!ret)
    ++(insn->readerCursor);

  return ret;
}

/*
 * lookAtByte - Like consumeByte, but does not advance the cursor.
 *
 * @param insn  - See consumeByte().
 * @param byte  - See consumeByte().
 * @return      - See consumeByte().
 */
static int lookAtByte(struct InternalInstruction* insn, uint8_t* byte) {
  return insn->reader(insn->readerArg, byte, insn->readerCursor);
}

static void unconsumeByte(struct InternalInstruction* insn) {
  insn->readerCursor--;
}

#define CONSUME_FUNC(name, type)                                  \
  static int name(struct InternalInstruction* insn, type* ptr) {  \
    type combined = 0;                                            \
    unsigned offset;                                              \
    for (offset = 0; offset < sizeof(type); ++offset) {           \
      uint8_t byte;                                               \
      int ret = insn->reader(insn->readerArg,                     \
                             &byte,                               \
                             insn->readerCursor + offset);        \
      if (ret)                                                    \
        return ret;                                               \
      combined = combined | ((uint64_t)byte << (offset * 8));     \
    }                                                             \
    *ptr = combined;                                              \
    insn->readerCursor += sizeof(type);                           \
    return 0;                                                     \
  }

/*
 * consume* - Use the reader function provided by the user to consume data
 *   values of various sizes from the instruction's memory and advance the
 *   cursor appropriately.  These readers perform endian conversion.
 *
 * @param insn    - See consumeByte().
 * @param ptr     - A pointer to a pre-allocated memory of appropriate size to
 *                  be populated with the data read.
 * @return        - See consumeByte().
 */
CONSUME_FUNC(consumeInt8, int8_t)
CONSUME_FUNC(consumeInt16, int16_t)
CONSUME_FUNC(consumeInt32, int32_t)
CONSUME_FUNC(consumeUInt16, uint16_t)
CONSUME_FUNC(consumeUInt32, uint32_t)
CONSUME_FUNC(consumeUInt64, uint64_t)

/*
 * dbgprintf - Uses the logging function provided by the user to log a single
 *   message, typically without a carriage-return.
 *
 * @param insn    - The instruction containing the logging function.
 * @param format  - See printf().
 * @param ...     - See printf().
 */
static void dbgprintf(struct InternalInstruction* insn,
                      const char* format,
                      ...) {
  char buffer[256];
  va_list ap;

  if (!insn->dlog)
    return;

  va_start(ap, format);
  (void)vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);

  insn->dlog(insn->dlogArg, buffer);
}

static bool isREX(struct InternalInstruction *insn, uint8_t prefix) {
  if (insn->mode == MODE_64BIT)
    return prefix >= 0x40 && prefix <= 0x4f;
  return false;
}

/*
 * setPrefixPresent - Marks that a particular prefix is present as mandatory
 *
 * @param insn      - The instruction to be marked as having the prefix.
 * @param prefix    - The prefix that is present.
 */
static void setPrefixPresent(struct InternalInstruction *insn, uint8_t prefix) {
  uint8_t nextByte;
  switch (prefix) {
  case 0xf0:
    insn->hasLockPrefix = true;
    break;
  case 0xf2:
  case 0xf3:
    if (lookAtByte(insn, &nextByte))
      break;
    // TODO:
    //  1. There could be several 0x66
    //  2. if (nextByte == 0x66) and nextNextByte != 0x0f then
    //      it's not mandatory prefix
    //  3. if (nextByte >= 0x40 && nextByte <= 0x4f) it's REX and we need
    //     0x0f exactly after it to be mandatory prefix
    if (isREX(insn, nextByte) || nextByte == 0x0f || nextByte == 0x66)
      // The last of 0xf2 /0xf3 is mandatory prefix
      insn->mandatoryPrefix = prefix;
    insn->repeatPrefix = prefix;
    break;
  case 0x66:
    if (lookAtByte(insn, &nextByte))
      break;
    // 0x66 can't overwrite existing mandatory prefix and should be ignored
    if (!insn->mandatoryPrefix && (nextByte == 0x0f || isREX(insn, nextByte)))
      insn->mandatoryPrefix = prefix;
    break;
  }
}

/*
 * readPrefixes - Consumes all of an instruction's prefix bytes, and marks the
 *   instruction as having them.  Also sets the instruction's default operand,
 *   address, and other relevant data sizes to report operands correctly.
 *
 * @param insn  - The instruction whose prefixes are to be read.
 * @return      - 0 if the instruction could be read until the end of the prefix
 *                bytes, and no prefixes conflicted; nonzero otherwise.
 */
static int readPrefixes(struct InternalInstruction* insn) {
  bool isPrefix = true;
  uint8_t byte = 0;
  uint8_t nextByte;

  dbgprintf(insn, "readPrefixes()");

  while (isPrefix) {
    /* If we fail reading prefixes, just stop here and let the opcode reader deal with it */
    if (consumeByte(insn, &byte))
      break;

    /*
     * If the byte is a LOCK/REP/REPNE prefix and not a part of the opcode, then
     * break and let it be disassembled as a normal "instruction".
     */
    if (insn->readerCursor - 1 == insn->startLocation && byte == 0xf0) // LOCK
      break;

    if ((byte == 0xf2 || byte == 0xf3) && !lookAtByte(insn, &nextByte)) {
      /*
       * If the byte is 0xf2 or 0xf3, and any of the following conditions are
       * met:
       * - it is followed by a LOCK (0xf0) prefix
       * - it is followed by an xchg instruction
       * then it should be disassembled as a xacquire/xrelease not repne/rep.
       */
      if (((nextByte == 0xf0) ||
           ((nextByte & 0xfe) == 0x86 || (nextByte & 0xf8) == 0x90))) {
        insn->xAcquireRelease = true;
        if (!(byte == 0xf3 && nextByte == 0x90)) // PAUSE instruction support
          break;
      }
      /*
       * Also if the byte is 0xf3, and the following condition is met:
       * - it is followed by a "mov mem, reg" (opcode 0x88/0x89) or
       *                       "mov mem, imm" (opcode 0xc6/0xc7) instructions.
       * then it should be disassembled as an xrelease not rep.
       */
      if (byte == 0xf3 && (nextByte == 0x88 || nextByte == 0x89 ||
                           nextByte == 0xc6 || nextByte == 0xc7)) {
        insn->xAcquireRelease = true;
        if (nextByte != 0x90) // PAUSE instruction support
          break;
      }
      if (isREX(insn, nextByte)) {
        uint8_t nnextByte;
        // Go to REX prefix after the current one
        if (consumeByte(insn, &nnextByte))
          return -1;
        // We should be able to read next byte after REX prefix
        if (lookAtByte(insn, &nnextByte))
          return -1;
        unconsumeByte(insn);
      }
    }

    switch (byte) {
    case 0xf0:  /* LOCK */
    case 0xf2:  /* REPNE/REPNZ */
    case 0xf3:  /* REP or REPE/REPZ */
      setPrefixPresent(insn, byte);
      break;
    case 0x2e:  /* CS segment override -OR- Branch not taken */
    case 0x36:  /* SS segment override -OR- Branch taken */
    case 0x3e:  /* DS segment override */
    case 0x26:  /* ES segment override */
    case 0x64:  /* FS segment override */
    case 0x65:  /* GS segment override */
      switch (byte) {
      case 0x2e:
        insn->segmentOverride = SEG_OVERRIDE_CS;
        break;
      case 0x36:
        insn->segmentOverride = SEG_OVERRIDE_SS;
        break;
      case 0x3e:
        insn->segmentOverride = SEG_OVERRIDE_DS;
        break;
      case 0x26:
        insn->segmentOverride = SEG_OVERRIDE_ES;
        break;
      case 0x64:
        insn->segmentOverride = SEG_OVERRIDE_FS;
        break;
      case 0x65:
        insn->segmentOverride = SEG_OVERRIDE_GS;
        break;
      default:
        debug("Unhandled override");
        return -1;
      }
      setPrefixPresent(insn, byte);
      break;
    case 0x66:  /* Operand-size override */
      insn->hasOpSize = true;
      setPrefixPresent(insn, byte);
      break;
    case 0x67:  /* Address-size override */
      insn->hasAdSize = true;
      setPrefixPresent(insn, byte);
      break;
    default:    /* Not a prefix byte */
      isPrefix = false;
      break;
    }

    if (isPrefix)
      dbgprintf(insn, "Found prefix 0x%hhx", byte);
  }

  insn->vectorExtensionType = TYPE_NO_VEX_XOP;

  if (byte == 0x62) {
    uint8_t byte1, byte2;

    if (consumeByte(insn, &byte1)) {
      dbgprintf(insn, "Couldn't read second byte of EVEX prefix");
      return -1;
    }

    if (lookAtByte(insn, &byte2)) {
      dbgprintf(insn, "Couldn't read third byte of EVEX prefix");
      return -1;
    }

    if ((insn->mode == MODE_64BIT || (byte1 & 0xc0) == 0xc0) &&
       ((~byte1 & 0xc) == 0xc) && ((byte2 & 0x4) == 0x4)) {
      insn->vectorExtensionType = TYPE_EVEX;
    } else {
      unconsumeByte(insn); /* unconsume byte1 */
      unconsumeByte(insn); /* unconsume byte  */
    }

    if (insn->vectorExtensionType == TYPE_EVEX) {
      insn->vectorExtensionPrefix[0] = byte;
      insn->vectorExtensionPrefix[1] = byte1;
      if (consumeByte(insn, &insn->vectorExtensionPrefix[2])) {
        dbgprintf(insn, "Couldn't read third byte of EVEX prefix");
        return -1;
      }
      if (consumeByte(insn, &insn->vectorExtensionPrefix[3])) {
        dbgprintf(insn, "Couldn't read fourth byte of EVEX prefix");
        return -1;
      }

      /* We simulate the REX prefix for simplicity's sake */
      if (insn->mode == MODE_64BIT) {
        insn->rexPrefix = 0x40
                        | (wFromEVEX3of4(insn->vectorExtensionPrefix[2]) << 3)
                        | (rFromEVEX2of4(insn->vectorExtensionPrefix[1]) << 2)
                        | (xFromEVEX2of4(insn->vectorExtensionPrefix[1]) << 1)
                        | (bFromEVEX2of4(insn->vectorExtensionPrefix[1]) << 0);
      }

      dbgprintf(insn, "Found EVEX prefix 0x%hhx 0x%hhx 0x%hhx 0x%hhx",
              insn->vectorExtensionPrefix[0], insn->vectorExtensionPrefix[1],
              insn->vectorExtensionPrefix[2], insn->vectorExtensionPrefix[3]);
    }
  } else if (byte == 0xc4) {
    uint8_t byte1;

    if (lookAtByte(insn, &byte1)) {
      dbgprintf(insn, "Couldn't read second byte of VEX");
      return -1;
    }

    if (insn->mode == MODE_64BIT || (byte1 & 0xc0) == 0xc0)
      insn->vectorExtensionType = TYPE_VEX_3B;
    else
      unconsumeByte(insn);

    if (insn->vectorExtensionType == TYPE_VEX_3B) {
      insn->vectorExtensionPrefix[0] = byte;
      consumeByte(insn, &insn->vectorExtensionPrefix[1]);
      consumeByte(insn, &insn->vectorExtensionPrefix[2]);

      /* We simulate the REX prefix for simplicity's sake */

      if (insn->mode == MODE_64BIT)
        insn->rexPrefix = 0x40
                        | (wFromVEX3of3(insn->vectorExtensionPrefix[2]) << 3)
                        | (rFromVEX2of3(insn->vectorExtensionPrefix[1]) << 2)
                        | (xFromVEX2of3(insn->vectorExtensionPrefix[1]) << 1)
                        | (bFromVEX2of3(insn->vectorExtensionPrefix[1]) << 0);

      dbgprintf(insn, "Found VEX prefix 0x%hhx 0x%hhx 0x%hhx",
                insn->vectorExtensionPrefix[0], insn->vectorExtensionPrefix[1],
                insn->vectorExtensionPrefix[2]);
    }
  } else if (byte == 0xc5) {
    uint8_t byte1;

    if (lookAtByte(insn, &byte1)) {
      dbgprintf(insn, "Couldn't read second byte of VEX");
      return -1;
    }

    if (insn->mode == MODE_64BIT || (byte1 & 0xc0) == 0xc0)
      insn->vectorExtensionType = TYPE_VEX_2B;
    else
      unconsumeByte(insn);

    if (insn->vectorExtensionType == TYPE_VEX_2B) {
      insn->vectorExtensionPrefix[0] = byte;
      consumeByte(insn, &insn->vectorExtensionPrefix[1]);

      if (insn->mode == MODE_64BIT)
        insn->rexPrefix = 0x40
                        | (rFromVEX2of2(insn->vectorExtensionPrefix[1]) << 2);

      switch (ppFromVEX2of2(insn->vectorExtensionPrefix[1])) {
      default:
        break;
      case VEX_PREFIX_66:
        insn->hasOpSize = true;
        break;
      }

      dbgprintf(insn, "Found VEX prefix 0x%hhx 0x%hhx",
                insn->vectorExtensionPrefix[0],
                insn->vectorExtensionPrefix[1]);
    }
  } else if (byte == 0x8f) {
    uint8_t byte1;

    if (lookAtByte(insn, &byte1)) {
      dbgprintf(insn, "Couldn't read second byte of XOP");
      return -1;
    }

    if ((byte1 & 0x38) != 0x0) /* 0 in these 3 bits is a POP instruction. */
      insn->vectorExtensionType = TYPE_XOP;
    else
      unconsumeByte(insn);

    if (insn->vectorExtensionType == TYPE_XOP) {
      insn->vectorExtensionPrefix[0] = byte;
      consumeByte(insn, &insn->vectorExtensionPrefix[1]);
      consumeByte(insn, &insn->vectorExtensionPrefix[2]);

      /* We simulate the REX prefix for simplicity's sake */

      if (insn->mode == MODE_64BIT)
        insn->rexPrefix = 0x40
                        | (wFromXOP3of3(insn->vectorExtensionPrefix[2]) << 3)
                        | (rFromXOP2of3(insn->vectorExtensionPrefix[1]) << 2)
                        | (xFromXOP2of3(insn->vectorExtensionPrefix[1]) << 1)
                        | (bFromXOP2of3(insn->vectorExtensionPrefix[1]) << 0);

      switch (ppFromXOP3of3(insn->vectorExtensionPrefix[2])) {
      default:
        break;
      case VEX_PREFIX_66:
        insn->hasOpSize = true;
        break;
      }

      dbgprintf(insn, "Found XOP prefix 0x%hhx 0x%hhx 0x%hhx",
                insn->vectorExtensionPrefix[0], insn->vectorExtensionPrefix[1],
                insn->vectorExtensionPrefix[2]);
    }
  } else if (isREX(insn, byte)) {
    if (lookAtByte(insn, &nextByte))
      return -1;
    insn->rexPrefix = byte;
    dbgprintf(insn, "Found REX prefix 0x%hhx", byte);
  } else
    unconsumeByte(insn);

  if (insn->mode == MODE_16BIT) {
    insn->registerSize = (insn->hasOpSize ? 4 : 2);
    insn->addressSize = (insn->hasAdSize ? 4 : 2);
    insn->displacementSize = (insn->hasAdSize ? 4 : 2);
    insn->immediateSize = (insn->hasOpSize ? 4 : 2);
  } else if (insn->mode == MODE_32BIT) {
    insn->registerSize = (insn->hasOpSize ? 2 : 4);
    insn->addressSize = (insn->hasAdSize ? 2 : 4);
    insn->displacementSize = (insn->hasAdSize ? 2 : 4);
    insn->immediateSize = (insn->hasOpSize ? 2 : 4);
  } else if (insn->mode == MODE_64BIT) {
    if (insn->rexPrefix && wFromREX(insn->rexPrefix)) {
      insn->registerSize       = 8;
      insn->addressSize = (insn->hasAdSize ? 4 : 8);
      insn->displacementSize   = 4;
      insn->immediateSize      = 4;
    } else {
      insn->registerSize = (insn->hasOpSize ? 2 : 4);
      insn->addressSize = (insn->hasAdSize ? 4 : 8);
      insn->displacementSize = (insn->hasOpSize ? 2 : 4);
      insn->immediateSize = (insn->hasOpSize ? 2 : 4);
    }
  }

  return 0;
}

static int readModRM(struct InternalInstruction* insn);

/*
 * readOpcode - Reads the opcode (excepting the ModR/M byte in the case of
 *   extended or escape opcodes).
 *
 * @param insn  - The instruction whose opcode is to be read.
 * @return      - 0 if the opcode could be read successfully; nonzero otherwise.
 */
static int readOpcode(struct InternalInstruction* insn) {
  /* Determine the length of the primary opcode */

  uint8_t current;

  dbgprintf(insn, "readOpcode()");

  insn->opcodeType = ONEBYTE;

  if (insn->vectorExtensionType == TYPE_EVEX) {
    switch (mmFromEVEX2of4(insn->vectorExtensionPrefix[1])) {
    default:
      dbgprintf(insn, "Unhandled mm field for instruction (0x%hhx)",
                mmFromEVEX2of4(insn->vectorExtensionPrefix[1]));
      return -1;
    case VEX_LOB_0F:
      insn->opcodeType = TWOBYTE;
      return consumeByte(insn, &insn->opcode);
    case VEX_LOB_0F38:
      insn->opcodeType = THREEBYTE_38;
      return consumeByte(insn, &insn->opcode);
    case VEX_LOB_0F3A:
      insn->opcodeType = THREEBYTE_3A;
      return consumeByte(insn, &insn->opcode);
    }
  } else if (insn->vectorExtensionType == TYPE_VEX_3B) {
    switch (mmmmmFromVEX2of3(insn->vectorExtensionPrefix[1])) {
    default:
      dbgprintf(insn, "Unhandled m-mmmm field for instruction (0x%hhx)",
                mmmmmFromVEX2of3(insn->vectorExtensionPrefix[1]));
      return -1;
    case VEX_LOB_0F:
      insn->opcodeType = TWOBYTE;
      return consumeByte(insn, &insn->opcode);
    case VEX_LOB_0F38:
      insn->opcodeType = THREEBYTE_38;
      return consumeByte(insn, &insn->opcode);
    case VEX_LOB_0F3A:
      insn->opcodeType = THREEBYTE_3A;
      return consumeByte(insn, &insn->opcode);
    }
  } else if (insn->vectorExtensionType == TYPE_VEX_2B) {
    insn->opcodeType = TWOBYTE;
    return consumeByte(insn, &insn->opcode);
  } else if (insn->vectorExtensionType == TYPE_XOP) {
    switch (mmmmmFromXOP2of3(insn->vectorExtensionPrefix[1])) {
    default:
      dbgprintf(insn, "Unhandled m-mmmm field for instruction (0x%hhx)",
                mmmmmFromVEX2of3(insn->vectorExtensionPrefix[1]));
      return -1;
    case XOP_MAP_SELECT_8:
      insn->opcodeType = XOP8_MAP;
      return consumeByte(insn, &insn->opcode);
    case XOP_MAP_SELECT_9:
      insn->opcodeType = XOP9_MAP;
      return consumeByte(insn, &insn->opcode);
    case XOP_MAP_SELECT_A:
      insn->opcodeType = XOPA_MAP;
      return consumeByte(insn, &insn->opcode);
    }
  }

  if (consumeByte(insn, &current))
    return -1;

  if (current == 0x0f) {
    dbgprintf(insn, "Found a two-byte escape prefix (0x%hhx)", current);

    if (consumeByte(insn, &current))
      return -1;

    if (current == 0x38) {
      dbgprintf(insn, "Found a three-byte escape prefix (0x%hhx)", current);

      if (consumeByte(insn, &current))
        return -1;

      insn->opcodeType = THREEBYTE_38;
    } else if (current == 0x3a) {
      dbgprintf(insn, "Found a three-byte escape prefix (0x%hhx)", current);

      if (consumeByte(insn, &current))
        return -1;

      insn->opcodeType = THREEBYTE_3A;
    } else if (current == 0x0f) {
      dbgprintf(insn, "Found a 3dnow escape prefix (0x%hhx)", current);

      // Consume operands before the opcode to comply with the 3DNow encoding
      if (readModRM(insn))
        return -1;

      if (consumeByte(insn, &current))
        return -1;

      insn->opcodeType = THREEDNOW_MAP;
    } else {
      dbgprintf(insn, "Didn't find a three-byte escape prefix");

      insn->opcodeType = TWOBYTE;
    }
  } else if (insn->mandatoryPrefix)
    // The opcode with mandatory prefix must start with opcode escape.
    // If not it's legacy repeat prefix
    insn->mandatoryPrefix = 0;

  /*
   * At this point we have consumed the full opcode.
   * Anything we consume from here on must be unconsumed.
   */

  insn->opcode = current;

  return 0;
}

/*
 * getIDWithAttrMask - Determines the ID of an instruction, consuming
 *   the ModR/M byte as appropriate for extended and escape opcodes,
 *   and using a supplied attribute mask.
 *
 * @param instructionID - A pointer whose target is filled in with the ID of the
 *                        instruction.
 * @param insn          - The instruction whose ID is to be determined.
 * @param attrMask      - The attribute mask to search.
 * @return              - 0 if the ModR/M could be read when needed or was not
 *                        needed; nonzero otherwise.
 */
static int getIDWithAttrMask(uint16_t* instructionID,
                             struct InternalInstruction* insn,
                             uint16_t attrMask) {
  bool hasModRMExtension;

  InstructionContext instructionClass = contextForAttrs(attrMask);

  hasModRMExtension = modRMRequired(insn->opcodeType,
                                    instructionClass,
                                    insn->opcode);

  if (hasModRMExtension) {
    if (readModRM(insn))
      return -1;

    *instructionID = decode(insn->opcodeType,
                            instructionClass,
                            insn->opcode,
                            insn->modRM);
  } else {
    *instructionID = decode(insn->opcodeType,
                            instructionClass,
                            insn->opcode,
                            0);
  }

  return 0;
}

/*
 * is16BitEquivalent - Determines whether two instruction names refer to
 * equivalent instructions but one is 16-bit whereas the other is not.
 *
 * @param orig  - The instruction that is not 16-bit
 * @param equiv - The instruction that is 16-bit
 */
static bool is16BitEquivalent(const char *orig, const char *equiv) {
  off_t i;

  for (i = 0;; i++) {
    if (orig[i] == '\0' && equiv[i] == '\0')
      return true;
    if (orig[i] == '\0' || equiv[i] == '\0')
      return false;
    if (orig[i] != equiv[i]) {
      if ((orig[i] == 'Q' || orig[i] == 'L') && equiv[i] == 'W')
        continue;
      if ((orig[i] == '6' || orig[i] == '3') && equiv[i] == '1')
        continue;
      if ((orig[i] == '4' || orig[i] == '2') && equiv[i] == '6')
        continue;
      return false;
    }
  }
}

/*
 * is64Bit - Determines whether this instruction is a 64-bit instruction.
 *
 * @param name - The instruction that is not 16-bit
 */
static bool is64Bit(const char *name) {
  off_t i;

  for (i = 0;; ++i) {
    if (name[i] == '\0')
      return false;
    if (name[i] == '6' && name[i+1] == '4')
      return true;
  }
}

/*
 * getID - Determines the ID of an instruction, consuming the ModR/M byte as
 *   appropriate for extended and escape opcodes.  Determines the attributes and
 *   context for the instruction before doing so.
 *
 * @param insn  - The instruction whose ID is to be determined.
 * @return      - 0 if the ModR/M could be read when needed or was not needed;
 *                nonzero otherwise.
 */
static int getID(struct InternalInstruction* insn, const void *miiArg) {
  uint16_t attrMask;
  uint16_t instructionID;

  dbgprintf(insn, "getID()");

  attrMask = ATTR_NONE;

  if (insn->mode == MODE_64BIT)
    attrMask |= ATTR_64BIT;

  if (insn->vectorExtensionType != TYPE_NO_VEX_XOP) {
    attrMask |= (insn->vectorExtensionType == TYPE_EVEX) ? ATTR_EVEX : ATTR_VEX;

    if (insn->vectorExtensionType == TYPE_EVEX) {
      switch (ppFromEVEX3of4(insn->vectorExtensionPrefix[2])) {
      case VEX_PREFIX_66:
        attrMask |= ATTR_OPSIZE;
        break;
      case VEX_PREFIX_F3:
        attrMask |= ATTR_XS;
        break;
      case VEX_PREFIX_F2:
        attrMask |= ATTR_XD;
        break;
      }

      if (zFromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_EVEXKZ;
      if (bFromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_EVEXB;
      if (aaaFromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_EVEXK;
      if (lFromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_EVEXL;
      if (l2FromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_EVEXL2;
    } else if (insn->vectorExtensionType == TYPE_VEX_3B) {
      switch (ppFromVEX3of3(insn->vectorExtensionPrefix[2])) {
      case VEX_PREFIX_66:
        attrMask |= ATTR_OPSIZE;
        break;
      case VEX_PREFIX_F3:
        attrMask |= ATTR_XS;
        break;
      case VEX_PREFIX_F2:
        attrMask |= ATTR_XD;
        break;
      }

      if (lFromVEX3of3(insn->vectorExtensionPrefix[2]))
        attrMask |= ATTR_VEXL;
    } else if (insn->vectorExtensionType == TYPE_VEX_2B) {
      switch (ppFromVEX2of2(insn->vectorExtensionPrefix[1])) {
      case VEX_PREFIX_66:
        attrMask |= ATTR_OPSIZE;
        break;
      case VEX_PREFIX_F3:
        attrMask |= ATTR_XS;
        break;
      case VEX_PREFIX_F2:
        attrMask |= ATTR_XD;
        break;
      }

      if (lFromVEX2of2(insn->vectorExtensionPrefix[1]))
        attrMask |= ATTR_VEXL;
    } else if (insn->vectorExtensionType == TYPE_XOP) {
      switch (ppFromXOP3of3(insn->vectorExtensionPrefix[2])) {
      case VEX_PREFIX_66:
        attrMask |= ATTR_OPSIZE;
        break;
      case VEX_PREFIX_F3:
        attrMask |= ATTR_XS;
        break;
      case VEX_PREFIX_F2:
        attrMask |= ATTR_XD;
        break;
      }

      if (lFromXOP3of3(insn->vectorExtensionPrefix[2]))
        attrMask |= ATTR_VEXL;
    } else {
      return -1;
    }
  } else if (!insn->mandatoryPrefix) {
    // If we don't have mandatory prefix we should use legacy prefixes here
    if (insn->hasOpSize && (insn->mode != MODE_16BIT))
      attrMask |= ATTR_OPSIZE;
    if (insn->hasAdSize)
      attrMask |= ATTR_ADSIZE;
    if (insn->opcodeType == ONEBYTE) {
      if (insn->repeatPrefix == 0xf3 && (insn->opcode == 0x90))
        // Special support for PAUSE
        attrMask |= ATTR_XS;
    } else {
      if (insn->repeatPrefix == 0xf2)
        attrMask |= ATTR_XD;
      else if (insn->repeatPrefix == 0xf3)
        attrMask |= ATTR_XS;
    }
  } else {
    switch (insn->mandatoryPrefix) {
    case 0xf2:
      attrMask |= ATTR_XD;
      break;
    case 0xf3:
      attrMask |= ATTR_XS;
      break;
    case 0x66:
      if (insn->mode != MODE_16BIT)
        attrMask |= ATTR_OPSIZE;
      break;
    case 0x67:
      attrMask |= ATTR_ADSIZE;
      break;
    }

  }

  if (insn->rexPrefix & 0x08) {
    attrMask |= ATTR_REXW;
    attrMask &= ~ATTR_ADSIZE;
  }

  /*
   * JCXZ/JECXZ need special handling for 16-bit mode because the meaning
   * of the AdSize prefix is inverted w.r.t. 32-bit mode.
   */
  if (insn->mode == MODE_16BIT && insn->opcodeType == ONEBYTE &&
      insn->opcode == 0xE3)
    attrMask ^= ATTR_ADSIZE;

  // If we're in 16-bit mode and this is one of the relative jumps and opsize
  // prefix isn't present, we need to force the opsize attribute since the
  // prefix is inverted relative to 32-bit mode.
  if (insn->mode == MODE_16BIT && !insn->hasOpSize &&
      insn->opcodeType == ONEBYTE &&
      (insn->opcode == 0xE8 || insn->opcode == 0xE9))
    attrMask |= ATTR_OPSIZE;

  if (insn->mode == MODE_16BIT && !insn->hasOpSize &&
      insn->opcodeType == TWOBYTE &&
      insn->opcode >= 0x80 && insn->opcode <= 0x8F)
    attrMask |= ATTR_OPSIZE;

  if (getIDWithAttrMask(&instructionID, insn, attrMask))
    return -1;

  /* The following clauses compensate for limitations of the tables. */

  if (insn->mode != MODE_64BIT &&
      insn->vectorExtensionType != TYPE_NO_VEX_XOP) {
    /*
     * The tables can't distinquish between cases where the W-bit is used to
     * select register size and cases where its a required part of the opcode.
     */
    if ((insn->vectorExtensionType == TYPE_EVEX &&
         wFromEVEX3of4(insn->vectorExtensionPrefix[2])) ||
        (insn->vectorExtensionType == TYPE_VEX_3B &&
         wFromVEX3of3(insn->vectorExtensionPrefix[2])) ||
        (insn->vectorExtensionType == TYPE_XOP &&
         wFromXOP3of3(insn->vectorExtensionPrefix[2]))) {

      uint16_t instructionIDWithREXW;
      if (getIDWithAttrMask(&instructionIDWithREXW,
                            insn, attrMask | ATTR_REXW)) {
        insn->instructionID = instructionID;
        insn->spec = specifierForUID(instructionID);
        return 0;
      }

      auto SpecName = GetInstrName(instructionIDWithREXW, miiArg);
      // If not a 64-bit instruction. Switch the opcode.
      if (!is64Bit(SpecName.data())) {
        insn->instructionID = instructionIDWithREXW;
        insn->spec = specifierForUID(instructionIDWithREXW);
        return 0;
      }
    }
  }

  /*
   * Absolute moves, umonitor, and movdir64b need special handling.
   * -For 16-bit mode because the meaning of the AdSize and OpSize prefixes are
   *  inverted w.r.t.
   * -For 32-bit mode we need to ensure the ADSIZE prefix is observed in
   *  any position.
   */
  if ((insn->opcodeType == ONEBYTE && ((insn->opcode & 0xFC) == 0xA0)) ||
      (insn->opcodeType == TWOBYTE && (insn->opcode == 0xAE)) ||
      (insn->opcodeType == THREEBYTE_38 && insn->opcode == 0xF8)) {
    /* Make sure we observed the prefixes in any position. */
    if (insn->hasAdSize)
      attrMask |= ATTR_ADSIZE;
    if (insn->hasOpSize)
      attrMask |= ATTR_OPSIZE;

    /* In 16-bit, invert the attributes. */
    if (insn->mode == MODE_16BIT) {
      attrMask ^= ATTR_ADSIZE;

      /* The OpSize attribute is only valid with the absolute moves. */
      if (insn->opcodeType == ONEBYTE && ((insn->opcode & 0xFC) == 0xA0))
        attrMask ^= ATTR_OPSIZE;
    }

    if (getIDWithAttrMask(&instructionID, insn, attrMask))
      return -1;

    insn->instructionID = instructionID;
    insn->spec = specifierForUID(instructionID);
    return 0;
  }

  if ((insn->mode == MODE_16BIT || insn->hasOpSize) &&
      !(attrMask & ATTR_OPSIZE)) {
    /*
     * The instruction tables make no distinction between instructions that
     * allow OpSize anywhere (i.e., 16-bit operations) and that need it in a
     * particular spot (i.e., many MMX operations).  In general we're
     * conservative, but in the specific case where OpSize is present but not
     * in the right place we check if there's a 16-bit operation.
     */

    const struct InstructionSpecifier *spec;
    uint16_t instructionIDWithOpsize;
    llvm::StringRef specName, specWithOpSizeName;

    spec = specifierForUID(instructionID);

    if (getIDWithAttrMask(&instructionIDWithOpsize,
                          insn,
                          attrMask | ATTR_OPSIZE)) {
      /*
       * ModRM required with OpSize but not present; give up and return version
       * without OpSize set
       */

      insn->instructionID = instructionID;
      insn->spec = spec;
      return 0;
    }

    specName = GetInstrName(instructionID, miiArg);
    specWithOpSizeName = GetInstrName(instructionIDWithOpsize, miiArg);

    if (is16BitEquivalent(specName.data(), specWithOpSizeName.data()) &&
        (insn->mode == MODE_16BIT) ^ insn->hasOpSize) {
      insn->instructionID = instructionIDWithOpsize;
      insn->spec = specifierForUID(instructionIDWithOpsize);
    } else {
      insn->instructionID = instructionID;
      insn->spec = spec;
    }
    return 0;
  }

  if (insn->opcodeType == ONEBYTE && insn->opcode == 0x90 &&
      insn->rexPrefix & 0x01) {
    /*
     * NOOP shouldn't decode as NOOP if REX.b is set. Instead
     * it should decode as XCHG %r8, %eax.
     */

    const struct InstructionSpecifier *spec;
    uint16_t instructionIDWithNewOpcode;
    const struct InstructionSpecifier *specWithNewOpcode;

    spec = specifierForUID(instructionID);

    /* Borrow opcode from one of the other XCHGar opcodes */
    insn->opcode = 0x91;

    if (getIDWithAttrMask(&instructionIDWithNewOpcode,
                          insn,
                          attrMask)) {
      insn->opcode = 0x90;

      insn->instructionID = instructionID;
      insn->spec = spec;
      return 0;
    }

    specWithNewOpcode = specifierForUID(instructionIDWithNewOpcode);

    /* Change back */
    insn->opcode = 0x90;

    insn->instructionID = instructionIDWithNewOpcode;
    insn->spec = specWithNewOpcode;

    return 0;
  }

  insn->instructionID = instructionID;
  insn->spec = specifierForUID(insn->instructionID);

  return 0;
}

/*
 * readSIB - Consumes the SIB byte to determine addressing information for an
 *   instruction.
 *
 * @param insn  - The instruction whose SIB byte is to be read.
 * @return      - 0 if the SIB byte was successfully read; nonzero otherwise.
 */
static int readSIB(struct InternalInstruction* insn) {
  SIBBase sibBaseBase = SIB_BASE_NONE;
  uint8_t index, base;

  dbgprintf(insn, "readSIB()");

  if (insn->consumedSIB)
    return 0;

  insn->consumedSIB = true;

  switch (insn->addressSize) {
  case 2:
    dbgprintf(insn, "SIB-based addressing doesn't work in 16-bit mode");
    return -1;
  case 4:
    insn->sibIndexBase = SIB_INDEX_EAX;
    sibBaseBase = SIB_BASE_EAX;
    break;
  case 8:
    insn->sibIndexBase = SIB_INDEX_RAX;
    sibBaseBase = SIB_BASE_RAX;
    break;
  }

  if (consumeByte(insn, &insn->sib))
    return -1;

  index = indexFromSIB(insn->sib) | (xFromREX(insn->rexPrefix) << 3);

  if (index == 0x4) {
    insn->sibIndex = SIB_INDEX_NONE;
  } else {
    insn->sibIndex = (SIBIndex)(insn->sibIndexBase + index);
  }

  insn->sibScale = 1 << scaleFromSIB(insn->sib);

  base = baseFromSIB(insn->sib) | (bFromREX(insn->rexPrefix) << 3);

  switch (base) {
  case 0x5:
  case 0xd:
    switch (modFromModRM(insn->modRM)) {
    case 0x0:
      insn->eaDisplacement = EA_DISP_32;
      insn->sibBase = SIB_BASE_NONE;
      break;
    case 0x1:
      insn->eaDisplacement = EA_DISP_8;
      insn->sibBase = (SIBBase)(sibBaseBase + base);
      break;
    case 0x2:
      insn->eaDisplacement = EA_DISP_32;
      insn->sibBase = (SIBBase)(sibBaseBase + base);
      break;
    case 0x3:
      debug("Cannot have Mod = 0b11 and a SIB byte");
      return -1;
    }
    break;
  default:
    insn->sibBase = (SIBBase)(sibBaseBase + base);
    break;
  }

  return 0;
}

/*
 * readDisplacement - Consumes the displacement of an instruction.
 *
 * @param insn  - The instruction whose displacement is to be read.
 * @return      - 0 if the displacement byte was successfully read; nonzero
 *                otherwise.
 */
static int readDisplacement(struct InternalInstruction* insn) {
  int8_t d8;
  int16_t d16;
  int32_t d32;

  dbgprintf(insn, "readDisplacement()");

  if (insn->consumedDisplacement)
    return 0;

  insn->consumedDisplacement = true;
  insn->displacementOffset = insn->readerCursor - insn->startLocation;

  switch (insn->eaDisplacement) {
  case EA_DISP_NONE:
    insn->consumedDisplacement = false;
    break;
  case EA_DISP_8:
    if (consumeInt8(insn, &d8))
      return -1;
    insn->displacement = d8;
    break;
  case EA_DISP_16:
    if (consumeInt16(insn, &d16))
      return -1;
    insn->displacement = d16;
    break;
  case EA_DISP_32:
    if (consumeInt32(insn, &d32))
      return -1;
    insn->displacement = d32;
    break;
  }

  insn->consumedDisplacement = true;
  return 0;
}

/*
 * readModRM - Consumes all addressing information (ModR/M byte, SIB byte, and
 *   displacement) for an instruction and interprets it.
 *
 * @param insn  - The instruction whose addressing information is to be read.
 * @return      - 0 if the information was successfully read; nonzero otherwise.
 */
static int readModRM(struct InternalInstruction* insn) {
  uint8_t mod, rm, reg, evexrm;

  dbgprintf(insn, "readModRM()");

  if (insn->consumedModRM)
    return 0;

  if (consumeByte(insn, &insn->modRM))
    return -1;
  insn->consumedModRM = true;

  mod     = modFromModRM(insn->modRM);
  rm      = rmFromModRM(insn->modRM);
  reg     = regFromModRM(insn->modRM);

  /*
   * This goes by insn->registerSize to pick the correct register, which messes
   * up if we're using (say) XMM or 8-bit register operands.  That gets fixed in
   * fixupReg().
   */
  switch (insn->registerSize) {
  case 2:
    insn->regBase = MODRM_REG_AX;
    insn->eaRegBase = EA_REG_AX;
    break;
  case 4:
    insn->regBase = MODRM_REG_EAX;
    insn->eaRegBase = EA_REG_EAX;
    break;
  case 8:
    insn->regBase = MODRM_REG_RAX;
    insn->eaRegBase = EA_REG_RAX;
    break;
  }

  reg |= rFromREX(insn->rexPrefix) << 3;
  rm  |= bFromREX(insn->rexPrefix) << 3;

  evexrm = 0;
  if (insn->vectorExtensionType == TYPE_EVEX && insn->mode == MODE_64BIT) {
    reg |= r2FromEVEX2of4(insn->vectorExtensionPrefix[1]) << 4;
    evexrm = xFromEVEX2of4(insn->vectorExtensionPrefix[1]) << 4;
  }

  insn->reg = (Reg)(insn->regBase + reg);

  switch (insn->addressSize) {
  case 2: {
    EABase eaBaseBase = EA_BASE_BX_SI;

    switch (mod) {
    case 0x0:
      if (rm == 0x6) {
        insn->eaBase = EA_BASE_NONE;
        insn->eaDisplacement = EA_DISP_16;
        if (readDisplacement(insn))
          return -1;
      } else {
        insn->eaBase = (EABase)(eaBaseBase + rm);
        insn->eaDisplacement = EA_DISP_NONE;
      }
      break;
    case 0x1:
      insn->eaBase = (EABase)(eaBaseBase + rm);
      insn->eaDisplacement = EA_DISP_8;
      insn->displacementSize = 1;
      if (readDisplacement(insn))
        return -1;
      break;
    case 0x2:
      insn->eaBase = (EABase)(eaBaseBase + rm);
      insn->eaDisplacement = EA_DISP_16;
      if (readDisplacement(insn))
        return -1;
      break;
    case 0x3:
      insn->eaBase = (EABase)(insn->eaRegBase + rm);
      if (readDisplacement(insn))
        return -1;
      break;
    }
    break;
  }
  case 4:
  case 8: {
    EABase eaBaseBase = (insn->addressSize == 4 ? EA_BASE_EAX : EA_BASE_RAX);

    switch (mod) {
    case 0x0:
      insn->eaDisplacement = EA_DISP_NONE; /* readSIB may override this */
      // In determining whether RIP-relative mode is used (rm=5),
      // or whether a SIB byte is present (rm=4),
      // the extension bits (REX.b and EVEX.x) are ignored.
      switch (rm & 7) {
      case 0x4: // SIB byte is present
        insn->eaBase = (insn->addressSize == 4 ?
                        EA_BASE_sib : EA_BASE_sib64);
        if (readSIB(insn) || readDisplacement(insn))
          return -1;
        break;
      case 0x5: // RIP-relative
        insn->eaBase = EA_BASE_NONE;
        insn->eaDisplacement = EA_DISP_32;
        if (readDisplacement(insn))
          return -1;
        break;
      default:
        insn->eaBase = (EABase)(eaBaseBase + rm);
        break;
      }
      break;
    case 0x1:
      insn->displacementSize = 1;
      LLVM_FALLTHROUGH;
    case 0x2:
      insn->eaDisplacement = (mod == 0x1 ? EA_DISP_8 : EA_DISP_32);
      switch (rm & 7) {
      case 0x4: // SIB byte is present
        insn->eaBase = EA_BASE_sib;
        if (readSIB(insn) || readDisplacement(insn))
          return -1;
        break;
      default:
        insn->eaBase = (EABase)(eaBaseBase + rm);
        if (readDisplacement(insn))
          return -1;
        break;
      }
      break;
    case 0x3:
      insn->eaDisplacement = EA_DISP_NONE;
      insn->eaBase = (EABase)(insn->eaRegBase + rm + evexrm);
      break;
    }
    break;
  }
  } /* switch (insn->addressSize) */

  return 0;
}

#define GENERIC_FIXUP_FUNC(name, base, prefix, mask)      \
  static uint16_t name(struct InternalInstruction *insn,  \
                       OperandType type,                  \
                       uint8_t index,                     \
                       uint8_t *valid) {                  \
    *valid = 1;                                           \
    switch (type) {                                       \
    default:                                              \
      debug("Unhandled register type");                   \
      *valid = 0;                                         \
      return 0;                                           \
    case TYPE_Rv:                                         \
      return base + index;                                \
    case TYPE_R8:                                         \
      index &= mask;                                      \
      if (index > 0xf)                                    \
        *valid = 0;                                       \
      if (insn->rexPrefix &&                              \
         index >= 4 && index <= 7) {                      \
        return prefix##_SPL + (index - 4);                \
      } else {                                            \
        return prefix##_AL + index;                       \
      }                                                   \
    case TYPE_R16:                                        \
      index &= mask;                                      \
      if (index > 0xf)                                    \
        *valid = 0;                                       \
      return prefix##_AX + index;                         \
    case TYPE_R32:                                        \
      index &= mask;                                      \
      if (index > 0xf)                                    \
        *valid = 0;                                       \
      return prefix##_EAX + index;                        \
    case TYPE_R64:                                        \
      index &= mask;                                      \
      if (index > 0xf)                                    \
        *valid = 0;                                       \
      return prefix##_RAX + index;                        \
    case TYPE_ZMM:                                        \
      return prefix##_ZMM0 + index;                       \
    case TYPE_YMM:                                        \
      return prefix##_YMM0 + index;                       \
    case TYPE_XMM:                                        \
      return prefix##_XMM0 + index;                       \
    case TYPE_VK:                                         \
      index &= 0xf;                                       \
      if (index > 7)                                      \
        *valid = 0;                                       \
      return prefix##_K0 + index;                         \
    case TYPE_MM64:                                       \
      return prefix##_MM0 + (index & 0x7);                \
    case TYPE_SEGMENTREG:                                 \
      if ((index & 7) > 5)                                \
        *valid = 0;                                       \
      return prefix##_ES + (index & 7);                   \
    case TYPE_DEBUGREG:                                   \
      return prefix##_DR0 + index;                        \
    case TYPE_CONTROLREG:                                 \
      return prefix##_CR0 + index;                        \
    case TYPE_BNDR:                                       \
      if (index > 3)                                      \
        *valid = 0;                                       \
      return prefix##_BND0 + index;                       \
    case TYPE_MVSIBX:                                     \
      return prefix##_XMM0 + index;                       \
    case TYPE_MVSIBY:                                     \
      return prefix##_YMM0 + index;                       \
    case TYPE_MVSIBZ:                                     \
      return prefix##_ZMM0 + index;                       \
    }                                                     \
  }

/*
 * fixup*Value - Consults an operand type to determine the meaning of the
 *   reg or R/M field.  If the operand is an XMM operand, for example, an
 *   operand would be XMM0 instead of AX, which readModRM() would otherwise
 *   misinterpret it as.
 *
 * @param insn  - The instruction containing the operand.
 * @param type  - The operand type.
 * @param index - The existing value of the field as reported by readModRM().
 * @param valid - The address of a uint8_t.  The target is set to 1 if the
 *                field is valid for the register class; 0 if not.
 * @return      - The proper value.
 */
GENERIC_FIXUP_FUNC(fixupRegValue, insn->regBase,    MODRM_REG, 0x1f)
GENERIC_FIXUP_FUNC(fixupRMValue,  insn->eaRegBase,  EA_REG,    0xf)

/*
 * fixupReg - Consults an operand specifier to determine which of the
 *   fixup*Value functions to use in correcting readModRM()'ss interpretation.
 *
 * @param insn  - See fixup*Value().
 * @param op    - The operand specifier.
 * @return      - 0 if fixup was successful; -1 if the register returned was
 *                invalid for its class.
 */
static int fixupReg(struct InternalInstruction *insn,
                    const struct OperandSpecifier *op) {
  uint8_t valid;

  dbgprintf(insn, "fixupReg()");

  switch ((OperandEncoding)op->encoding) {
  default:
    debug("Expected a REG or R/M encoding in fixupReg");
    return -1;
  case ENCODING_VVVV:
    insn->vvvv = (Reg)fixupRegValue(insn,
                                    (OperandType)op->type,
                                    insn->vvvv,
                                    &valid);
    if (!valid)
      return -1;
    break;
  case ENCODING_REG:
    insn->reg = (Reg)fixupRegValue(insn,
                                   (OperandType)op->type,
                                   insn->reg - insn->regBase,
                                   &valid);
    if (!valid)
      return -1;
    break;
  CASE_ENCODING_RM:
    if (insn->eaBase >= insn->eaRegBase) {
      insn->eaBase = (EABase)fixupRMValue(insn,
                                          (OperandType)op->type,
                                          insn->eaBase - insn->eaRegBase,
                                          &valid);
      if (!valid)
        return -1;
    }
    break;
  }

  return 0;
}

/*
 * readOpcodeRegister - Reads an operand from the opcode field of an
 *   instruction and interprets it appropriately given the operand width.
 *   Handles AddRegFrm instructions.
 *
 * @param insn  - the instruction whose opcode field is to be read.
 * @param size  - The width (in bytes) of the register being specified.
 *                1 means AL and friends, 2 means AX, 4 means EAX, and 8 means
 *                RAX.
 * @return      - 0 on success; nonzero otherwise.
 */
static int readOpcodeRegister(struct InternalInstruction* insn, uint8_t size) {
  dbgprintf(insn, "readOpcodeRegister()");

  if (size == 0)
    size = insn->registerSize;

  switch (size) {
  case 1:
    insn->opcodeRegister = (Reg)(MODRM_REG_AL + ((bFromREX(insn->rexPrefix) << 3)
                                                  | (insn->opcode & 7)));
    if (insn->rexPrefix &&
        insn->opcodeRegister >= MODRM_REG_AL + 0x4 &&
        insn->opcodeRegister < MODRM_REG_AL + 0x8) {
      insn->opcodeRegister = (Reg)(MODRM_REG_SPL
                                   + (insn->opcodeRegister - MODRM_REG_AL - 4));
    }

    break;
  case 2:
    insn->opcodeRegister = (Reg)(MODRM_REG_AX
                                 + ((bFromREX(insn->rexPrefix) << 3)
                                    | (insn->opcode & 7)));
    break;
  case 4:
    insn->opcodeRegister = (Reg)(MODRM_REG_EAX
                                 + ((bFromREX(insn->rexPrefix) << 3)
                                    | (insn->opcode & 7)));
    break;
  case 8:
    insn->opcodeRegister = (Reg)(MODRM_REG_RAX
                                 + ((bFromREX(insn->rexPrefix) << 3)
                                    | (insn->opcode & 7)));
    break;
  }

  return 0;
}

/*
 * readImmediate - Consumes an immediate operand from an instruction, given the
 *   desired operand size.
 *
 * @param insn  - The instruction whose operand is to be read.
 * @param size  - The width (in bytes) of the operand.
 * @return      - 0 if the immediate was successfully consumed; nonzero
 *                otherwise.
 */
static int readImmediate(struct InternalInstruction* insn, uint8_t size) {
  uint8_t imm8;
  uint16_t imm16;
  uint32_t imm32;
  uint64_t imm64;

  dbgprintf(insn, "readImmediate()");

  if (insn->numImmediatesConsumed == 2) {
    debug("Already consumed two immediates");
    return -1;
  }

  if (size == 0)
    size = insn->immediateSize;
  else
    insn->immediateSize = size;
  insn->immediateOffset = insn->readerCursor - insn->startLocation;

  switch (size) {
  case 1:
    if (consumeByte(insn, &imm8))
      return -1;
    insn->immediates[insn->numImmediatesConsumed] = imm8;
    break;
  case 2:
    if (consumeUInt16(insn, &imm16))
      return -1;
    insn->immediates[insn->numImmediatesConsumed] = imm16;
    break;
  case 4:
    if (consumeUInt32(insn, &imm32))
      return -1;
    insn->immediates[insn->numImmediatesConsumed] = imm32;
    break;
  case 8:
    if (consumeUInt64(insn, &imm64))
      return -1;
    insn->immediates[insn->numImmediatesConsumed] = imm64;
    break;
  }

  insn->numImmediatesConsumed++;

  return 0;
}

/*
 * readVVVV - Consumes vvvv from an instruction if it has a VEX prefix.
 *
 * @param insn  - The instruction whose operand is to be read.
 * @return      - 0 if the vvvv was successfully consumed; nonzero
 *                otherwise.
 */
static int readVVVV(struct InternalInstruction* insn) {
  dbgprintf(insn, "readVVVV()");

  int vvvv;
  if (insn->vectorExtensionType == TYPE_EVEX)
    vvvv = (v2FromEVEX4of4(insn->vectorExtensionPrefix[3]) << 4 |
            vvvvFromEVEX3of4(insn->vectorExtensionPrefix[2]));
  else if (insn->vectorExtensionType == TYPE_VEX_3B)
    vvvv = vvvvFromVEX3of3(insn->vectorExtensionPrefix[2]);
  else if (insn->vectorExtensionType == TYPE_VEX_2B)
    vvvv = vvvvFromVEX2of2(insn->vectorExtensionPrefix[1]);
  else if (insn->vectorExtensionType == TYPE_XOP)
    vvvv = vvvvFromXOP3of3(insn->vectorExtensionPrefix[2]);
  else
    return -1;

  if (insn->mode != MODE_64BIT)
    vvvv &= 0xf; // Can only clear bit 4. Bit 3 must be cleared later.

  insn->vvvv = static_cast<Reg>(vvvv);
  return 0;
}

/*
 * readMaskRegister - Reads an mask register from the opcode field of an
 *   instruction.
 *
 * @param insn    - The instruction whose opcode field is to be read.
 * @return        - 0 on success; nonzero otherwise.
 */
static int readMaskRegister(struct InternalInstruction* insn) {
  dbgprintf(insn, "readMaskRegister()");

  if (insn->vectorExtensionType != TYPE_EVEX)
    return -1;

  insn->writemask =
      static_cast<Reg>(aaaFromEVEX4of4(insn->vectorExtensionPrefix[3]));
  return 0;
}

/*
 * readOperands - Consults the specifier for an instruction and consumes all
 *   operands for that instruction, interpreting them as it goes.
 *
 * @param insn  - The instruction whose operands are to be read and interpreted.
 * @return      - 0 if all operands could be read; nonzero otherwise.
 */
static int readOperands(struct InternalInstruction* insn) {
  int hasVVVV, needVVVV;
  int sawRegImm = 0;

  dbgprintf(insn, "readOperands()");

  /* If non-zero vvvv specified, need to make sure one of the operands
     uses it. */
  hasVVVV = !readVVVV(insn);
  needVVVV = hasVVVV && (insn->vvvv != 0);

  for (const auto &Op : x86OperandSets[insn->spec->operands]) {
    switch (Op.encoding) {
    case ENCODING_NONE:
    case ENCODING_SI:
    case ENCODING_DI:
      break;
    CASE_ENCODING_VSIB:
      // VSIB can use the V2 bit so check only the other bits.
      if (needVVVV)
        needVVVV = hasVVVV & ((insn->vvvv & 0xf) != 0);
      if (readModRM(insn))
        return -1;

      // Reject if SIB wasn't used.
      if (insn->eaBase != EA_BASE_sib && insn->eaBase != EA_BASE_sib64)
        return -1;

      // If sibIndex was set to SIB_INDEX_NONE, index offset is 4.
      if (insn->sibIndex == SIB_INDEX_NONE)
        insn->sibIndex = (SIBIndex)(insn->sibIndexBase + 4);

      // If EVEX.v2 is set this is one of the 16-31 registers.
      if (insn->vectorExtensionType == TYPE_EVEX && insn->mode == MODE_64BIT &&
          v2FromEVEX4of4(insn->vectorExtensionPrefix[3]))
        insn->sibIndex = (SIBIndex)(insn->sibIndex + 16);

      // Adjust the index register to the correct size.
      switch ((OperandType)Op.type) {
      default:
        debug("Unhandled VSIB index type");
        return -1;
      case TYPE_MVSIBX:
        insn->sibIndex = (SIBIndex)(SIB_INDEX_XMM0 +
                                    (insn->sibIndex - insn->sibIndexBase));
        break;
      case TYPE_MVSIBY:
        insn->sibIndex = (SIBIndex)(SIB_INDEX_YMM0 +
                                    (insn->sibIndex - insn->sibIndexBase));
        break;
      case TYPE_MVSIBZ:
        insn->sibIndex = (SIBIndex)(SIB_INDEX_ZMM0 +
                                    (insn->sibIndex - insn->sibIndexBase));
        break;
      }

      // Apply the AVX512 compressed displacement scaling factor.
      if (Op.encoding != ENCODING_REG && insn->eaDisplacement == EA_DISP_8)
        insn->displacement *= 1 << (Op.encoding - ENCODING_VSIB);
      break;
    case ENCODING_REG:
    CASE_ENCODING_RM:
      if (readModRM(insn))
        return -1;
      if (fixupReg(insn, &Op))
        return -1;
      // Apply the AVX512 compressed displacement scaling factor.
      if (Op.encoding != ENCODING_REG && insn->eaDisplacement == EA_DISP_8)
        insn->displacement *= 1 << (Op.encoding - ENCODING_RM);
      break;
    case ENCODING_IB:
      if (sawRegImm) {
        /* Saw a register immediate so don't read again and instead split the
           previous immediate.  FIXME: This is a hack. */
        insn->immediates[insn->numImmediatesConsumed] =
          insn->immediates[insn->numImmediatesConsumed - 1] & 0xf;
        ++insn->numImmediatesConsumed;
        break;
      }
      if (readImmediate(insn, 1))
        return -1;
      if (Op.type == TYPE_XMM || Op.type == TYPE_YMM)
        sawRegImm = 1;
      break;
    case ENCODING_IW:
      if (readImmediate(insn, 2))
        return -1;
      break;
    case ENCODING_ID:
      if (readImmediate(insn, 4))
        return -1;
      break;
    case ENCODING_IO:
      if (readImmediate(insn, 8))
        return -1;
      break;
    case ENCODING_Iv:
      if (readImmediate(insn, insn->immediateSize))
        return -1;
      break;
    case ENCODING_Ia:
      if (readImmediate(insn, insn->addressSize))
        return -1;
      break;
    case ENCODING_IRC:
      insn->RC = (l2FromEVEX4of4(insn->vectorExtensionPrefix[3]) << 1) |
                 lFromEVEX4of4(insn->vectorExtensionPrefix[3]);
      break;
    case ENCODING_RB:
      if (readOpcodeRegister(insn, 1))
        return -1;
      break;
    case ENCODING_RW:
      if (readOpcodeRegister(insn, 2))
        return -1;
      break;
    case ENCODING_RD:
      if (readOpcodeRegister(insn, 4))
        return -1;
      break;
    case ENCODING_RO:
      if (readOpcodeRegister(insn, 8))
        return -1;
      break;
    case ENCODING_Rv:
      if (readOpcodeRegister(insn, 0))
        return -1;
      break;
    case ENCODING_FP:
      break;
    case ENCODING_VVVV:
      needVVVV = 0; /* Mark that we have found a VVVV operand. */
      if (!hasVVVV)
        return -1;
      if (insn->mode != MODE_64BIT)
        insn->vvvv = static_cast<Reg>(insn->vvvv & 0x7);
      if (fixupReg(insn, &Op))
        return -1;
      break;
    case ENCODING_WRITEMASK:
      if (readMaskRegister(insn))
        return -1;
      break;
    case ENCODING_DUP:
      break;
    default:
      dbgprintf(insn, "Encountered an operand with an unknown encoding.");
      return -1;
    }
  }

  /* If we didn't find ENCODING_VVVV operand, but non-zero vvvv present, fail */
  if (needVVVV) return -1;

  return 0;
}

/*
 * decodeInstruction - Reads and interprets a full instruction provided by the
 *   user.
 *
 * @param insn      - A pointer to the instruction to be populated.  Must be
 *                    pre-allocated.
 * @param reader    - The function to be used to read the instruction's bytes.
 * @param readerArg - A generic argument to be passed to the reader to store
 *                    any internal state.
 * @param logger    - If non-NULL, the function to be used to write log messages
 *                    and warnings.
 * @param loggerArg - A generic argument to be passed to the logger to store
 *                    any internal state.
 * @param startLoc  - The address (in the reader's address space) of the first
 *                    byte in the instruction.
 * @param mode      - The mode (real mode, IA-32e, or IA-32e in 64-bit mode) to
 *                    decode the instruction in.
 * @return          - 0 if the instruction's memory could be read; nonzero if
 *                    not.
 */
int llvm::X86Disassembler::decodeInstruction(
    struct InternalInstruction *insn, byteReader_t reader,
    const void *readerArg, dlog_t logger, void *loggerArg, const void *miiArg,
    uint64_t startLoc, DisassemblerMode mode) {
  memset(insn, 0, sizeof(struct InternalInstruction));

  insn->reader = reader;
  insn->readerArg = readerArg;
  insn->dlog = logger;
  insn->dlogArg = loggerArg;
  insn->startLocation = startLoc;
  insn->readerCursor = startLoc;
  insn->mode = mode;
  insn->numImmediatesConsumed = 0;

  if (readPrefixes(insn)       ||
      readOpcode(insn)         ||
      getID(insn, miiArg)      ||
      insn->instructionID == 0 ||
      readOperands(insn))
    return -1;

  insn->operands = x86OperandSets[insn->spec->operands];

  insn->length = insn->readerCursor - insn->startLocation;

  dbgprintf(insn, "Read from 0x%llx to 0x%llx: length %zu",
            startLoc, insn->readerCursor, insn->length);

  if (insn->length > 15)
    dbgprintf(insn, "Instruction exceeds 15-byte limit");

  return 0;
}
