//==- HexagonInstrFormatsV65.td - Hexagon Instruction Formats -*- tablegen -==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the Hexagon V60 instruction classes in TableGen format.
//
//===----------------------------------------------------------------------===//

//----------------------------------------------------------------------------//
//                         Hexagon Intruction Flags +
//
//                        *** Must match BaseInfo.h ***
//----------------------------------------------------------------------------//

//----------------------------------------------------------------------------//
//                         Intruction Classes Definitions +
//----------------------------------------------------------------------------//

class CVI_VA_Resource_NoOpcode<dag outs, dag ins, string asmstr,
                       list<dag> pattern = [], string cstr = "",
                       InstrItinClass itin = CVI_VA>
   : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeCVI_VA>;

class CVI_GATHER_TMP_LD_Resource_NoOpcode<dag outs, dag ins, string asmstr,
                        list<dag> pattern = [], string cstr = "",
                        InstrItinClass itin = CVI_GATHER_PSEUDO>
   : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeCVI_GATHER>;
