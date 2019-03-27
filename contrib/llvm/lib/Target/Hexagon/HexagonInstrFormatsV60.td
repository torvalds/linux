//==- HexagonInstrFormatsV60.td - Hexagon Instruction Formats -*- tablegen -==//
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
//                         Instruction Classes Definitions +
//----------------------------------------------------------------------------//

class CVI_VA_Resource<dag outs, dag ins, string asmstr,
                       list<dag> pattern = [], string cstr = "",
                       InstrItinClass itin = CVI_VA>
   : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeCVI_VA>,
     OpcodeHexagon, Requires<[HasV60, UseHVX]>;
