//===--- HexagonMapAsm2IntrinV65.gen.td -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def: Pat<(int_hexagon_A6_vcmpbeq_notany DoubleRegs:$src1, DoubleRegs:$src2), (A6_vcmpbeq_notany DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV65]>;
def: Pat<(int_hexagon_V6_vasruwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3), (V6_vasruwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vasruwuhsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3), (V6_vasruwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vasruhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3), (V6_vasruhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vasruhubsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3), (V6_vasruhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vasruhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3), (V6_vasruhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vasruhubrndsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3), (V6_vasruhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vabsb HvxVR:$src1), (V6_vabsb HvxVR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vabsb_128B HvxVR:$src1), (V6_vabsb HvxVR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vabsb_sat HvxVR:$src1), (V6_vabsb_sat HvxVR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vabsb_sat_128B HvxVR:$src1), (V6_vabsb_sat HvxVR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vaslh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3), (V6_vaslh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vaslh_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3), (V6_vaslh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vasrh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3), (V6_vasrh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vasrh_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3), (V6_vasrh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vavguw HvxVR:$src1, HvxVR:$src2), (V6_vavguw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vavguw_128B HvxVR:$src1, HvxVR:$src2), (V6_vavguw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vavguwrnd HvxVR:$src1, HvxVR:$src2), (V6_vavguwrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vavguwrnd_128B HvxVR:$src1, HvxVR:$src2), (V6_vavguwrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vavgb HvxVR:$src1, HvxVR:$src2), (V6_vavgb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vavgb_128B HvxVR:$src1, HvxVR:$src2), (V6_vavgb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vavgbrnd HvxVR:$src1, HvxVR:$src2), (V6_vavgbrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vavgbrnd_128B HvxVR:$src1, HvxVR:$src2), (V6_vavgbrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vlut4 HvxVR:$src1, DoubleRegs:$src2), (V6_vlut4 HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vlut4_128B HvxVR:$src1, DoubleRegs:$src2), (V6_vlut4 HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vnavgb HvxVR:$src1, HvxVR:$src2), (V6_vnavgb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vnavgb_128B HvxVR:$src1, HvxVR:$src2), (V6_vnavgb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpabuu HvxWR:$src1, IntRegs:$src2), (V6_vmpabuu HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpabuu_128B HvxWR:$src1, IntRegs:$src2), (V6_vmpabuu HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpabuu_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3), (V6_vmpabuu_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpabuu_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3), (V6_vmpabuu_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpahhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vmpahhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpahhsat_128B HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vmpahhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpauhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vmpauhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpauhuhsat_128B HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vmpauhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpsuhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vmpsuhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpsuhuhsat_128B HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vmpsuhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpyh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3), (V6_vmpyh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpyh_acc_128B HvxWR:$src1, HvxVR:$src2, IntRegs:$src3), (V6_vmpyh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpyuhe HvxVR:$src1, IntRegs:$src2), (V6_vmpyuhe HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpyuhe_128B HvxVR:$src1, IntRegs:$src2), (V6_vmpyuhe HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpyuhe_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3), (V6_vmpyuhe_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vmpyuhe_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3), (V6_vmpyuhe_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vprefixqb HvxQR:$src1), (V6_vprefixqb HvxQR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vprefixqb_128B HvxQR:$src1), (V6_vprefixqb HvxQR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vprefixqh HvxQR:$src1), (V6_vprefixqh HvxQR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vprefixqh_128B HvxQR:$src1), (V6_vprefixqh HvxQR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vprefixqw HvxQR:$src1), (V6_vprefixqw HvxQR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vprefixqw_128B HvxQR:$src1), (V6_vprefixqw HvxQR:$src1)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vrmpyub_rtt HvxVR:$src1, DoubleRegs:$src2), (V6_vrmpyub_rtt HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vrmpyub_rtt_128B HvxVR:$src1, DoubleRegs:$src2), (V6_vrmpyub_rtt HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vrmpyub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vrmpyub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vrmpyub_rtt_acc_128B HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vrmpyub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vrmpybub_rtt HvxVR:$src1, DoubleRegs:$src2), (V6_vrmpybub_rtt HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vrmpybub_rtt_128B HvxVR:$src1, DoubleRegs:$src2), (V6_vrmpybub_rtt HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vrmpybub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vrmpybub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vrmpybub_rtt_acc_128B HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3), (V6_vrmpybub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermw IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4), (V6_vscattermw IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermh IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4), (V6_vscattermh IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermw_add IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4), (V6_vscattermw_add IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermh_add IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4), (V6_vscattermh_add IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermwq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxVR:$src4, HvxVR:$src5), (V6_vscattermwq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxVR:$src4, HvxVR:$src5)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermhq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxVR:$src4, HvxVR:$src5), (V6_vscattermhq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxVR:$src4, HvxVR:$src5)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermhw IntRegs:$src1, ModRegs:$src2, HvxWR:$src3, HvxVR:$src4), (V6_vscattermhw IntRegs:$src1, ModRegs:$src2, HvxWR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermhw_add IntRegs:$src1, ModRegs:$src2, HvxWR:$src3, HvxVR:$src4), (V6_vscattermhw_add IntRegs:$src1, ModRegs:$src2, HvxWR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermhwq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxWR:$src4, HvxVR:$src5), (V6_vscattermhwq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxWR:$src4, HvxVR:$src5)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermw_128B IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4), (V6_vscattermw IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermh_128B IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4), (V6_vscattermh IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermw_add_128B IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4), (V6_vscattermw_add IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermh_add_128B IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4), (V6_vscattermh_add IntRegs:$src1, ModRegs:$src2, HvxVR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermwq_128B HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxVR:$src4, HvxVR:$src5), (V6_vscattermwq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxVR:$src4, HvxVR:$src5)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermhq_128B HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxVR:$src4, HvxVR:$src5), (V6_vscattermhq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxVR:$src4, HvxVR:$src5)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermhw_128B IntRegs:$src1, ModRegs:$src2, HvxWR:$src3, HvxVR:$src4), (V6_vscattermhw IntRegs:$src1, ModRegs:$src2, HvxWR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermhw_add_128B IntRegs:$src1, ModRegs:$src2, HvxWR:$src3, HvxVR:$src4), (V6_vscattermhw_add IntRegs:$src1, ModRegs:$src2, HvxWR:$src3, HvxVR:$src4)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vscattermhwq_128B HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxWR:$src4, HvxVR:$src5), (V6_vscattermhwq HvxQR:$src1, IntRegs:$src2, ModRegs:$src3, HvxWR:$src4, HvxVR:$src5)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vdd0),      (V6_vdd0)>, Requires<[HasV65, UseHVX]>;
def: Pat<(int_hexagon_V6_vdd0_128B), (V6_vdd0)>, Requires<[HasV65, UseHVX]>;
