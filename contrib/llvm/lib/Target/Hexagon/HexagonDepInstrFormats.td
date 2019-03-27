//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Automatically generated file, please consult code owner before editing.
//===----------------------------------------------------------------------===//

class Enc_890909 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <2> Pe4;
  let Inst{6-5} = Pe4{1-0};
}
class Enc_9be1de : OpcodeHexagon {
  bits <2> Qs4;
  let Inst{6-5} = Qs4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vv32;
  let Inst{12-8} = Vv32{4-0};
  bits <5> Vw32;
  let Inst{4-0} = Vw32{4-0};
}
class Enc_527412 : OpcodeHexagon {
  bits <2> Ps4;
  let Inst{17-16} = Ps4{1-0};
  bits <2> Pt4;
  let Inst{9-8} = Pt4{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_efaed8 : OpcodeHexagon {
  bits <1> Ii;
  let Inst{8-8} = Ii{0-0};
}
class Enc_a568d4 : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_27b757 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
}
class Enc_1de724 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <4> n1;
  let Inst{28-28} = n1{3-3};
  let Inst{24-22} = n1{2-0};
}
class Enc_0e41fa : OpcodeHexagon {
  bits <5> Vuu32;
  let Inst{12-8} = Vuu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_3d6d37 : OpcodeHexagon {
  bits <2> Qs4;
  let Inst{6-5} = Qs4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vvv32;
  let Inst{12-8} = Vvv32{4-0};
  bits <5> Vw32;
  let Inst{4-0} = Vw32{4-0};
}
class Enc_a641d0 : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vvv32;
  let Inst{12-8} = Vvv32{4-0};
  bits <5> Vw32;
  let Inst{4-0} = Vw32{4-0};
}
class Enc_802dc0 : OpcodeHexagon {
  bits <1> Ii;
  let Inst{8-8} = Ii{0-0};
  bits <2> Qv4;
  let Inst{23-22} = Qv4{1-0};
}
class Enc_6b197f : OpcodeHexagon {
  bits <4> Ii;
  let Inst{8-5} = Ii{3-0};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_51436c : OpcodeHexagon {
  bits <16> Ii;
  let Inst{23-22} = Ii{15-14};
  let Inst{13-0} = Ii{13-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_c7a204 : OpcodeHexagon {
  bits <6> II;
  let Inst{5-0} = II{5-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Re32;
  let Inst{20-16} = Re32{4-0};
}
class Enc_db40cd : OpcodeHexagon {
  bits <6> Ii;
  let Inst{6-3} = Ii{5-2};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_a1e29d : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> II;
  let Inst{22-21} = II{4-3};
  let Inst{7-5} = II{2-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_d15d19 : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_e90a15 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <4> n1;
  let Inst{29-29} = n1{3-3};
  let Inst{26-25} = n1{2-1};
  let Inst{22-22} = n1{0-0};
}
class Enc_e0a47a : OpcodeHexagon {
  bits <4> Ii;
  let Inst{8-5} = Ii{3-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_140c83 : OpcodeHexagon {
  bits <10> Ii;
  let Inst{21-21} = Ii{9-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_7eee72 : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_310ba1 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rtt32;
  let Inst{20-16} = Rtt32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_d7dc10 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_6baed4 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_736575 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <4> n1;
  let Inst{28-28} = n1{3-3};
  let Inst{25-23} = n1{2-0};
}
class Enc_8dec2e : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_28dcbb : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vvv32;
  let Inst{4-0} = Vvv32{4-0};
}
class Enc_eaa9f8 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <2> Qx4;
  let Inst{1-0} = Qx4{1-0};
}
class Enc_509701 : OpcodeHexagon {
  bits <19> Ii;
  let Inst{26-25} = Ii{18-17};
  let Inst{20-16} = Ii{16-12};
  let Inst{13-5} = Ii{11-3};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_830e5d : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <8> II;
  let Inst{22-16} = II{7-1};
  let Inst{13-13} = II{0-0};
  bits <2> Pu4;
  let Inst{24-23} = Pu4{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_79b8c8 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{6-3} = Ii{5-2};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_58a8bf : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_041d7b : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <5> n1;
  let Inst{28-28} = n1{4-4};
  let Inst{24-23} = n1{3-2};
  let Inst{13-13} = n1{1-1};
  let Inst{8-8} = n1{0-0};
}
class Enc_f44229 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{13-13} = Ii{6-6};
  let Inst{7-3} = Ii{5-1};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_aad80c : OpcodeHexagon {
  bits <5> Vuu32;
  let Inst{12-8} = Vuu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_87c142 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{8-4} = Ii{6-2};
  bits <4> Rt16;
  let Inst{3-0} = Rt16{3-0};
}
class Enc_86a14b : OpcodeHexagon {
  bits <8> Ii;
  let Inst{7-3} = Ii{7-3};
  bits <3> Rdd8;
  let Inst{2-0} = Rdd8{2-0};
}
class Enc_9a33d5 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{6-3} = Ii{6-3};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_a56825 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_9ea4cf : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{6-6} = Ii{0-0};
  bits <6> II;
  let Inst{5-0} = II{5-0};
  bits <5> Ru32;
  let Inst{20-16} = Ru32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_ee5ed0 : OpcodeHexagon {
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
  bits <2> n1;
  let Inst{9-8} = n1{1-0};
}
class Enc_bddee3 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vyyyy32;
  let Inst{4-0} = Vyyyy32{4-0};
  bits <3> Rx8;
  let Inst{18-16} = Rx8{2-0};
}
class Enc_935d9b : OpcodeHexagon {
  bits <5> Ii;
  let Inst{6-3} = Ii{4-1};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_61f0b0 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rxx32;
  let Inst{4-0} = Rxx32{4-0};
}
class Enc_bd6011 : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_65d691 : OpcodeHexagon {
  bits <2> Ps4;
  let Inst{17-16} = Ps4{1-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_e8c45e : OpcodeHexagon {
  bits <7> Ii;
  let Inst{13-13} = Ii{6-6};
  let Inst{7-3} = Ii{5-1};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_ca3887 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_a94f3b : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <2> Pe4;
  let Inst{6-5} = Pe4{1-0};
}
class Enc_625deb : OpcodeHexagon {
  bits <4> Ii;
  let Inst{10-8} = Ii{3-1};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rt16;
  let Inst{3-0} = Rt16{3-0};
}
class Enc_1f5ba6 : OpcodeHexagon {
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_cd82bc : OpcodeHexagon {
  bits <4> Ii;
  let Inst{21-21} = Ii{3-3};
  let Inst{7-5} = Ii{2-0};
  bits <6> II;
  let Inst{13-8} = II{5-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_399e12 : OpcodeHexagon {
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <3> Rdd8;
  let Inst{2-0} = Rdd8{2-0};
}
class Enc_d7a65e : OpcodeHexagon {
  bits <6> Ii;
  let Inst{12-7} = Ii{5-0};
  bits <6> II;
  let Inst{13-13} = II{5-5};
  let Inst{4-0} = II{4-0};
  bits <2> Pv4;
  let Inst{6-5} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_607661 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{12-7} = Ii{5-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_6a5972 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <4> Rt16;
  let Inst{11-8} = Rt16{3-0};
}
class Enc_ff3442 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
}
class Enc_53dca9 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{11-8} = Ii{5-2};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_27fd0e : OpcodeHexagon {
  bits <6> Ii;
  let Inst{8-5} = Ii{5-2};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_d7bc34 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <3> Rt8;
  let Inst{18-16} = Rt8{2-0};
  bits <5> Vyyyy32;
  let Inst{4-0} = Vyyyy32{4-0};
}
class Enc_93af4c : OpcodeHexagon {
  bits <7> Ii;
  let Inst{10-4} = Ii{6-0};
  bits <4> Rx16;
  let Inst{3-0} = Rx16{3-0};
}
class Enc_621fba : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Gd32;
  let Inst{4-0} = Gd32{4-0};
}
class Enc_5bdd42 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{8-5} = Ii{6-3};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_ad9bef : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rtt32;
  let Inst{20-16} = Rtt32{4-0};
  bits <5> Vxx32;
  let Inst{4-0} = Vxx32{4-0};
}
class Enc_71f1b4 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{8-5} = Ii{5-2};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_14640c : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <5> n1;
  let Inst{28-28} = n1{4-4};
  let Inst{24-22} = n1{3-1};
  let Inst{13-13} = n1{0-0};
}
class Enc_31db33 : OpcodeHexagon {
  bits <2> Qt4;
  let Inst{6-5} = Qt4{1-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_65f095 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{6-3} = Ii{5-2};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_784502 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <3> Os8;
  let Inst{2-0} = Os8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_6413b6 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <5> n1;
  let Inst{29-29} = n1{4-4};
  let Inst{26-25} = n1{3-2};
  let Inst{23-23} = n1{1-1};
  let Inst{13-13} = n1{0-0};
}
class Enc_7a0ea6 : OpcodeHexagon {
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
  bits <1> n1;
  let Inst{9-9} = n1{0-0};
}
class Enc_84bff1 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_f4413a : OpcodeHexagon {
  bits <4> Ii;
  let Inst{8-5} = Ii{3-0};
  bits <2> Pt4;
  let Inst{10-9} = Pt4{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_78e566 : OpcodeHexagon {
  bits <2> Pt4;
  let Inst{9-8} = Pt4{1-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_437f33 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <2> Pu4;
  let Inst{6-5} = Pu4{1-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_0527db : OpcodeHexagon {
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rx16;
  let Inst{3-0} = Rx16{3-0};
}
class Enc_420cf3 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{22-21} = Ii{5-4};
  let Inst{13-13} = Ii{3-3};
  let Inst{7-5} = Ii{2-0};
  bits <5> Ru32;
  let Inst{4-0} = Ru32{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{12-8} = Rd32{4-0};
}
class Enc_e39bb2 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{9-4} = Ii{5-0};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_1b64fb : OpcodeHexagon {
  bits <16> Ii;
  let Inst{26-25} = Ii{15-14};
  let Inst{20-16} = Ii{13-9};
  let Inst{13-13} = Ii{8-8};
  let Inst{7-0} = Ii{7-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_c1d806 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
  bits <2> Qe4;
  let Inst{6-5} = Qe4{1-0};
}
class Enc_c6220b : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ru32;
  let Inst{12-8} = Ru32{4-0};
  bits <3> Nt8;
  let Inst{2-0} = Nt8{2-0};
}
class Enc_322e1b : OpcodeHexagon {
  bits <6> Ii;
  let Inst{22-21} = Ii{5-4};
  let Inst{13-13} = Ii{3-3};
  let Inst{7-5} = Ii{2-0};
  bits <6> II;
  let Inst{23-23} = II{5-5};
  let Inst{4-0} = II{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{12-8} = Rd32{4-0};
}
class Enc_989021 : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vy32;
  let Inst{12-8} = Vy32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_178717 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <6> n1;
  let Inst{28-28} = n1{5-5};
  let Inst{25-23} = n1{4-2};
  let Inst{13-13} = n1{1-1};
  let Inst{8-8} = n1{0-0};
}
class Enc_78cbf0 : OpcodeHexagon {
  bits <18> Ii;
  let Inst{26-25} = Ii{17-16};
  let Inst{20-16} = Ii{15-11};
  let Inst{13-13} = Ii{10-10};
  let Inst{7-0} = Ii{9-2};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_052c7d : OpcodeHexagon {
  bits <5> Ii;
  let Inst{6-3} = Ii{4-1};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_fcf7a7 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_55355c : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ru32;
  let Inst{12-8} = Ru32{4-0};
  bits <5> Rtt32;
  let Inst{4-0} = Rtt32{4-0};
}
class Enc_211aaa : OpcodeHexagon {
  bits <11> Ii;
  let Inst{26-25} = Ii{10-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_6185fe : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <6> II;
  let Inst{11-8} = II{5-2};
  let Inst{6-5} = II{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_74aef2 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{8-5} = Ii{3-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_cd4705 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{7-5} = Ii{2-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_2ebe3b : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_3d5b28 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_5ab2be : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_fef969 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{20-16} = Ii{5-1};
  let Inst{5-5} = Ii{0-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_63eaeb : OpcodeHexagon {
  bits <2> Ii;
  let Inst{1-0} = Ii{1-0};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
}
class Enc_95441f : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <2> Qd4;
  let Inst{1-0} = Qd4{1-0};
}
class Enc_372c9d : OpcodeHexagon {
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <3> Os8;
  let Inst{2-0} = Os8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_4dff07 : OpcodeHexagon {
  bits <2> Qv4;
  let Inst{12-11} = Qv4{1-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_04c959 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <6> II;
  let Inst{11-8} = II{5-2};
  let Inst{6-5} = II{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
}
class Enc_b62ef7 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_2b518f : OpcodeHexagon {
  bits <32> Ii;
  let Inst{27-16} = Ii{31-20};
  let Inst{13-0} = Ii{19-6};
}
class Enc_b388cf : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> II;
  let Inst{22-21} = II{4-3};
  let Inst{7-5} = II{2-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_ad1c74 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
}
class Enc_74d4e5 : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_c90aca : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_222336 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{8-5} = Ii{3-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_5e87ce : OpcodeHexagon {
  bits <16> Ii;
  let Inst{23-22} = Ii{15-14};
  let Inst{20-16} = Ii{13-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_158beb : OpcodeHexagon {
  bits <2> Qs4;
  let Inst{6-5} = Qs4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vv32;
  let Inst{4-0} = Vv32{4-0};
}
class Enc_f7ea77 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <4> n1;
  let Inst{29-29} = n1{3-3};
  let Inst{26-25} = n1{2-1};
  let Inst{13-13} = n1{0-0};
}
class Enc_245865 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{23-19} = Vv32{4-0};
  bits <3> Rt8;
  let Inst{18-16} = Rt8{2-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_88d4d9 : OpcodeHexagon {
  bits <2> Pu4;
  let Inst{9-8} = Pu4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_226535 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-7} = Ii{7-2};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{4-0} = Rt32{4-0};
}
class Enc_31aa6a : OpcodeHexagon {
  bits <5> Ii;
  let Inst{6-3} = Ii{4-1};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_397f23 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{13-13} = Ii{7-7};
  let Inst{7-3} = Ii{6-2};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_865390 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_98c0b8 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <2> Pv4;
  let Inst{6-5} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_bfbf03 : OpcodeHexagon {
  bits <2> Qs4;
  let Inst{9-8} = Qs4{1-0};
  bits <2> Qd4;
  let Inst{1-0} = Qd4{1-0};
}
class Enc_ecbcc8 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_f5e933 : OpcodeHexagon {
  bits <2> Ps4;
  let Inst{17-16} = Ps4{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_3fc427 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vxx32;
  let Inst{4-0} = Vxx32{4-0};
}
class Enc_01d3d0 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_b0e9d8 : OpcodeHexagon {
  bits <10> Ii;
  let Inst{21-21} = Ii{9-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_1bd127 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <3> Rt8;
  let Inst{18-16} = Rt8{2-0};
  bits <5> Vdddd32;
  let Inst{4-0} = Vdddd32{4-0};
}
class Enc_3694bd : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <5> n1;
  let Inst{29-29} = n1{4-4};
  let Inst{26-25} = n1{3-2};
  let Inst{23-22} = n1{1-0};
}
class Enc_a42857 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <5> n1;
  let Inst{28-28} = n1{4-4};
  let Inst{24-22} = n1{3-1};
  let Inst{8-8} = n1{0-0};
}
class Enc_b7fad3 : OpcodeHexagon {
  bits <2> Pv4;
  let Inst{9-8} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_223005 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{6-3} = Ii{5-2};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_9e4c3f : OpcodeHexagon {
  bits <6> II;
  let Inst{13-8} = II{5-0};
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rd16;
  let Inst{19-16} = Rd16{3-0};
}
class Enc_8b8d61 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{22-21} = Ii{5-4};
  let Inst{13-13} = Ii{3-3};
  let Inst{7-5} = Ii{2-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ru32;
  let Inst{4-0} = Ru32{4-0};
  bits <5> Rd32;
  let Inst{12-8} = Rd32{4-0};
}
class Enc_88c16c : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rxx32;
  let Inst{4-0} = Rxx32{4-0};
}
class Enc_770858 : OpcodeHexagon {
  bits <2> Ps4;
  let Inst{6-5} = Ps4{1-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_bd811a : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Cd32;
  let Inst{4-0} = Cd32{4-0};
}
class Enc_b05839 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{8-5} = Ii{6-3};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_bc03e5 : OpcodeHexagon {
  bits <17> Ii;
  let Inst{26-25} = Ii{16-15};
  let Inst{20-16} = Ii{14-10};
  let Inst{13-13} = Ii{9-9};
  let Inst{7-0} = Ii{8-1};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_412ff0 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Ru32;
  let Inst{4-0} = Ru32{4-0};
  bits <5> Rxx32;
  let Inst{12-8} = Rxx32{4-0};
}
class Enc_ef601b : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
}
class Enc_c9a18e : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_be32a5 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_e6abcf : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
}
class Enc_d6990d : OpcodeHexagon {
  bits <5> Vuu32;
  let Inst{12-8} = Vuu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vxx32;
  let Inst{4-0} = Vxx32{4-0};
}
class Enc_6c9440 : OpcodeHexagon {
  bits <10> Ii;
  let Inst{21-21} = Ii{9-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_0d8adb : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_50e578 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_1cf4ca : OpcodeHexagon {
  bits <6> Ii;
  let Inst{17-16} = Ii{5-4};
  let Inst{6-3} = Ii{3-0};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_48b75f : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_b97f71 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{8-5} = Ii{5-2};
  bits <2> Pt4;
  let Inst{10-9} = Pt4{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_9d1247 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{8-5} = Ii{6-3};
  bits <2> Pt4;
  let Inst{10-9} = Pt4{1-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_7b7ba8 : OpcodeHexagon {
  bits <2> Qu4;
  let Inst{9-8} = Qu4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_f7430e : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <3> Os8;
  let Inst{2-0} = Os8{2-0};
}
class Enc_e7581c : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_2301d6 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{20-16} = Ii{5-1};
  let Inst{8-8} = Ii{0-0};
  bits <2> Pt4;
  let Inst{10-9} = Pt4{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_c31910 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{23-21} = Ii{7-5};
  let Inst{13-13} = Ii{4-4};
  let Inst{7-5} = Ii{3-1};
  let Inst{3-3} = Ii{0-0};
  bits <5> II;
  let Inst{12-8} = II{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_2f2f04 : OpcodeHexagon {
  bits <1> Ii;
  let Inst{5-5} = Ii{0-0};
  bits <5> Vuu32;
  let Inst{12-8} = Vuu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_8d8a30 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_2d7491 : OpcodeHexagon {
  bits <13> Ii;
  let Inst{26-25} = Ii{12-11};
  let Inst{13-5} = Ii{10-2};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_a803e0 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{12-7} = Ii{6-1};
  bits <8> II;
  let Inst{13-13} = II{7-7};
  let Inst{6-0} = II{6-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_45364e : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_b909d2 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <7> n1;
  let Inst{28-28} = n1{6-6};
  let Inst{25-22} = n1{5-2};
  let Inst{13-13} = n1{1-1};
  let Inst{8-8} = n1{0-0};
}
class Enc_e6c957 : OpcodeHexagon {
  bits <10> Ii;
  let Inst{21-21} = Ii{9-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_0d8870 : OpcodeHexagon {
  bits <12> Ii;
  let Inst{26-25} = Ii{11-10};
  let Inst{13-13} = Ii{9-9};
  let Inst{7-0} = Ii{8-1};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_9fae8a : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-8} = Ii{5-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_18c338 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <8> II;
  let Inst{22-16} = II{7-1};
  let Inst{13-13} = II{0-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_5ccba9 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-7} = Ii{7-2};
  bits <6> II;
  let Inst{13-13} = II{5-5};
  let Inst{4-0} = II{4-0};
  bits <2> Pv4;
  let Inst{6-5} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_0ed752 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Cdd32;
  let Inst{4-0} = Cdd32{4-0};
}
class Enc_143445 : OpcodeHexagon {
  bits <13> Ii;
  let Inst{26-25} = Ii{12-11};
  let Inst{13-13} = Ii{10-10};
  let Inst{7-0} = Ii{9-2};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_3a3d62 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_3e3989 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <6> n1;
  let Inst{28-28} = n1{5-5};
  let Inst{25-22} = n1{4-1};
  let Inst{8-8} = n1{0-0};
}
class Enc_152467 : OpcodeHexagon {
  bits <5> Ii;
  let Inst{8-5} = Ii{4-1};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_9ac432 : OpcodeHexagon {
  bits <2> Ps4;
  let Inst{17-16} = Ps4{1-0};
  bits <2> Pt4;
  let Inst{9-8} = Pt4{1-0};
  bits <2> Pu4;
  let Inst{7-6} = Pu4{1-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_a90628 : OpcodeHexagon {
  bits <2> Qv4;
  let Inst{23-22} = Qv4{1-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_f37377 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-7} = Ii{7-2};
  bits <8> II;
  let Inst{13-13} = II{7-7};
  let Inst{6-0} = II{6-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_a198f6 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{10-5} = Ii{6-1};
  bits <2> Pt4;
  let Inst{12-11} = Pt4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_4e4a80 : OpcodeHexagon {
  bits <2> Qs4;
  let Inst{6-5} = Qs4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vvv32;
  let Inst{4-0} = Vvv32{4-0};
}
class Enc_3dac0b : OpcodeHexagon {
  bits <2> Qt4;
  let Inst{6-5} = Qt4{1-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_e38e1f : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <2> Pu4;
  let Inst{22-21} = Pu4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_f8ecf9 : OpcodeHexagon {
  bits <5> Vuu32;
  let Inst{12-8} = Vuu32{4-0};
  bits <5> Vvv32;
  let Inst{20-16} = Vvv32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_7f1a05 : OpcodeHexagon {
  bits <5> Ru32;
  let Inst{4-0} = Ru32{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ry32;
  let Inst{12-8} = Ry32{4-0};
}
class Enc_2df31d : OpcodeHexagon {
  bits <8> Ii;
  let Inst{9-4} = Ii{7-2};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_25bef0 : OpcodeHexagon {
  bits <16> Ii;
  let Inst{26-25} = Ii{15-14};
  let Inst{20-16} = Ii{13-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_f82302 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <4> n1;
  let Inst{29-29} = n1{3-3};
  let Inst{26-25} = n1{2-1};
  let Inst{23-23} = n1{0-0};
}
class Enc_44271f : OpcodeHexagon {
  bits <5> Gs32;
  let Inst{20-16} = Gs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_83ee64 : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_adf111 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <2> Qx4;
  let Inst{1-0} = Qx4{1-0};
}
class Enc_46c951 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{12-7} = Ii{5-0};
  bits <5> II;
  let Inst{4-0} = II{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_5d6c34 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-8} = Ii{5-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_4df4e9 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{26-25} = Ii{10-9};
  let Inst{13-13} = Ii{8-8};
  let Inst{7-0} = Ii{7-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_263841 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rtt32;
  let Inst{20-16} = Rtt32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_91b9fe : OpcodeHexagon {
  bits <5> Ii;
  let Inst{6-3} = Ii{4-1};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_a7b8e8 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{22-21} = Ii{5-4};
  let Inst{13-13} = Ii{3-3};
  let Inst{7-5} = Ii{2-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_2b3f60 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <2> Px4;
  let Inst{6-5} = Px4{1-0};
}
class Enc_bd1cbc : OpcodeHexagon {
  bits <5> Ii;
  let Inst{8-5} = Ii{4-1};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_c85e2a : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> II;
  let Inst{22-21} = II{4-3};
  let Inst{7-5} = II{2-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_a30110 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{23-19} = Vv32{4-0};
  bits <3> Rt8;
  let Inst{18-16} = Rt8{2-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_33f8ba : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-8} = Ii{7-3};
  let Inst{4-2} = Ii{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_690862 : OpcodeHexagon {
  bits <13> Ii;
  let Inst{26-25} = Ii{12-11};
  let Inst{13-13} = Ii{10-10};
  let Inst{7-0} = Ii{9-2};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_2a3787 : OpcodeHexagon {
  bits <13> Ii;
  let Inst{26-25} = Ii{12-11};
  let Inst{13-5} = Ii{10-2};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_d5c73f : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_3f97c8 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{6-3} = Ii{5-2};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_d50cd3 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{7-5} = Ii{2-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_729ff7 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{7-5} = Ii{2-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_217147 : OpcodeHexagon {
  bits <2> Qv4;
  let Inst{23-22} = Qv4{1-0};
}
class Enc_b9c5fb : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_f394d3 : OpcodeHexagon {
  bits <6> II;
  let Inst{11-8} = II{5-2};
  let Inst{6-5} = II{1-0};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
  bits <5> Re32;
  let Inst{20-16} = Re32{4-0};
}
class Enc_0cb018 : OpcodeHexagon {
  bits <5> Cs32;
  let Inst{20-16} = Cs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_541f26 : OpcodeHexagon {
  bits <18> Ii;
  let Inst{26-25} = Ii{17-16};
  let Inst{20-16} = Ii{15-11};
  let Inst{13-13} = Ii{10-10};
  let Inst{7-0} = Ii{9-2};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_724154 : OpcodeHexagon {
  bits <6> II;
  let Inst{5-0} = II{5-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Re32;
  let Inst{20-16} = Re32{4-0};
}
class Enc_179b35 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_585242 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-13} = Ii{5-5};
  let Inst{7-3} = Ii{4-0};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_cf1927 : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <3> Os8;
  let Inst{2-0} = Os8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_b84c4c : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-8} = Ii{5-0};
  bits <6> II;
  let Inst{23-21} = II{5-3};
  let Inst{7-5} = II{2-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_8203bb : OpcodeHexagon {
  bits <6> Ii;
  let Inst{12-7} = Ii{5-0};
  bits <8> II;
  let Inst{13-13} = II{7-7};
  let Inst{6-0} = II{6-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_e66a97 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{12-7} = Ii{6-1};
  bits <5> II;
  let Inst{4-0} = II{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_8c2412 : OpcodeHexagon {
  bits <2> Ps4;
  let Inst{6-5} = Ps4{1-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_284ebb : OpcodeHexagon {
  bits <2> Ps4;
  let Inst{17-16} = Ps4{1-0};
  bits <2> Pt4;
  let Inst{9-8} = Pt4{1-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_733b27 : OpcodeHexagon {
  bits <5> Ii;
  let Inst{8-5} = Ii{4-1};
  bits <2> Pt4;
  let Inst{10-9} = Pt4{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_22c845 : OpcodeHexagon {
  bits <14> Ii;
  let Inst{10-0} = Ii{13-3};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_ed5027 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Gdd32;
  let Inst{4-0} = Gdd32{4-0};
}
class Enc_9b0bc1 : OpcodeHexagon {
  bits <2> Pu4;
  let Inst{6-5} = Pu4{1-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_ea4c54 : OpcodeHexagon {
  bits <2> Pu4;
  let Inst{6-5} = Pu4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_b72622 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{5-5} = Ii{0-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rxx32;
  let Inst{4-0} = Rxx32{4-0};
}
class Enc_569cfe : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_96ce4f : OpcodeHexagon {
  bits <4> Ii;
  let Inst{6-3} = Ii{3-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_143a3c : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-8} = Ii{5-0};
  bits <6> II;
  let Inst{23-21} = II{5-3};
  let Inst{7-5} = II{2-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rxx32;
  let Inst{4-0} = Rxx32{4-0};
}
class Enc_57a33e : OpcodeHexagon {
  bits <9> Ii;
  let Inst{13-13} = Ii{8-8};
  let Inst{7-3} = Ii{7-3};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
}
class Enc_311abd : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_a1640c : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-8} = Ii{5-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_de0214 : OpcodeHexagon {
  bits <12> Ii;
  let Inst{26-25} = Ii{11-10};
  let Inst{13-5} = Ii{9-1};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_daea09 : OpcodeHexagon {
  bits <17> Ii;
  let Inst{23-22} = Ii{16-15};
  let Inst{20-16} = Ii{14-10};
  let Inst{13-13} = Ii{9-9};
  let Inst{7-1} = Ii{8-2};
  bits <2> Pu4;
  let Inst{9-8} = Pu4{1-0};
}
class Enc_fda92c : OpcodeHexagon {
  bits <17> Ii;
  let Inst{26-25} = Ii{16-15};
  let Inst{20-16} = Ii{14-10};
  let Inst{13-13} = Ii{9-9};
  let Inst{7-0} = Ii{8-1};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_831a7d : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rxx32;
  let Inst{4-0} = Rxx32{4-0};
  bits <2> Pe4;
  let Inst{6-5} = Pe4{1-0};
}
class Enc_11a146 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{11-8} = Ii{3-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_b15941 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{6-3} = Ii{3-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_b78edd : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <4> n1;
  let Inst{28-28} = n1{3-3};
  let Inst{24-23} = n1{2-1};
  let Inst{8-8} = n1{0-0};
}
class Enc_a27588 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{26-25} = Ii{10-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
}
class Enc_2a7b91 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{20-16} = Ii{5-1};
  let Inst{8-8} = Ii{0-0};
  bits <2> Pt4;
  let Inst{10-9} = Pt4{1-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_b43b67 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
  bits <2> Qx4;
  let Inst{6-5} = Qx4{1-0};
}
class Enc_4aca3a : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <3> n1;
  let Inst{29-29} = n1{2-2};
  let Inst{26-25} = n1{1-0};
}
class Enc_b38ffc : OpcodeHexagon {
  bits <4> Ii;
  let Inst{11-8} = Ii{3-0};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rt16;
  let Inst{3-0} = Rt16{3-0};
}
class Enc_cda00a : OpcodeHexagon {
  bits <12> Ii;
  let Inst{19-16} = Ii{11-8};
  let Inst{12-5} = Ii{7-0};
  bits <2> Pu4;
  let Inst{22-21} = Pu4{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_2fbf3c : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_70b24b : OpcodeHexagon {
  bits <6> Ii;
  let Inst{8-5} = Ii{5-2};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_2ae154 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_50b5ac : OpcodeHexagon {
  bits <6> Ii;
  let Inst{17-16} = Ii{5-4};
  let Inst{6-3} = Ii{3-0};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
}
class Enc_2ea740 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <2> Qv4;
  let Inst{12-11} = Qv4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
}
class Enc_08d755 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_1178da : OpcodeHexagon {
  bits <3> Ii;
  let Inst{7-5} = Ii{2-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vxx32;
  let Inst{4-0} = Vxx32{4-0};
}
class Enc_8dbe85 : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_5a18b3 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <5> n1;
  let Inst{29-29} = n1{4-4};
  let Inst{26-25} = n1{3-2};
  let Inst{22-22} = n1{1-1};
  let Inst{13-13} = n1{0-0};
}
class Enc_14d27a : OpcodeHexagon {
  bits <5> II;
  let Inst{12-8} = II{4-0};
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
}
class Enc_a05677 : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_f0cca7 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <6> II;
  let Inst{20-16} = II{5-1};
  let Inst{13-13} = II{0-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_500cb0 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vxx32;
  let Inst{4-0} = Vxx32{4-0};
}
class Enc_7e5a82 : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_12b6e9 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{11-8} = Ii{3-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_6f70ca : OpcodeHexagon {
  bits <8> Ii;
  let Inst{8-4} = Ii{7-3};
}
class Enc_7222b7 : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <2> Qd4;
  let Inst{1-0} = Qd4{1-0};
}
class Enc_e3b0c4 : OpcodeHexagon {

}
class Enc_a255dc : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_cb785b : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rtt32;
  let Inst{20-16} = Rtt32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_cb4b4e : OpcodeHexagon {
  bits <2> Pu4;
  let Inst{6-5} = Pu4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_1f5d8f : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_9cdba7 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_5cd7e9 : OpcodeHexagon {
  bits <12> Ii;
  let Inst{26-25} = Ii{11-10};
  let Inst{13-5} = Ii{9-1};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
}
class Enc_454a26 : OpcodeHexagon {
  bits <2> Pt4;
  let Inst{9-8} = Pt4{1-0};
  bits <2> Ps4;
  let Inst{17-16} = Ps4{1-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_a6853f : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
  bits <6> n1;
  let Inst{29-29} = n1{5-5};
  let Inst{26-25} = n1{4-3};
  let Inst{23-22} = n1{2-1};
  let Inst{13-13} = n1{0-0};
}
class Enc_c175d0 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{11-8} = Ii{3-0};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_16c48b : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vv32;
  let Inst{12-8} = Vv32{4-0};
  bits <5> Vw32;
  let Inst{4-0} = Vw32{4-0};
}
class Enc_895bd9 : OpcodeHexagon {
  bits <2> Qu4;
  let Inst{9-8} = Qu4{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_ea23e4 : OpcodeHexagon {
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_4dc228 : OpcodeHexagon {
  bits <9> Ii;
  let Inst{12-8} = Ii{8-4};
  let Inst{4-3} = Ii{3-2};
  bits <10> II;
  let Inst{20-16} = II{9-5};
  let Inst{7-5} = II{4-2};
  let Inst{1-0} = II{1-0};
}
class Enc_10bc21 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{6-3} = Ii{3-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_1aaec1 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <3> Os8;
  let Inst{2-0} = Os8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_329361 : OpcodeHexagon {
  bits <2> Pu4;
  let Inst{6-5} = Pu4{1-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_d2c7f1 : OpcodeHexagon {
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <2> Pe4;
  let Inst{6-5} = Pe4{1-0};
}
class Enc_3680c2 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{11-5} = Ii{6-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_1ef990 : OpcodeHexagon {
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_e957fb : OpcodeHexagon {
  bits <12> Ii;
  let Inst{26-25} = Ii{11-10};
  let Inst{13-13} = Ii{9-9};
  let Inst{7-0} = Ii{8-1};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_c0cdde : OpcodeHexagon {
  bits <9> Ii;
  let Inst{13-5} = Ii{8-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_c9e3bc : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
}
class Enc_2e1979 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <2> Pv4;
  let Inst{6-5} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_0b2e5b : OpcodeHexagon {
  bits <3> Ii;
  let Inst{7-5} = Ii{2-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_6f83e7 : OpcodeHexagon {
  bits <2> Qv4;
  let Inst{23-22} = Qv4{1-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_6339d5 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <2> Pv4;
  let Inst{6-5} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ru32;
  let Inst{12-8} = Ru32{4-0};
  bits <5> Rt32;
  let Inst{4-0} = Rt32{4-0};
}
class Enc_d483b9 : OpcodeHexagon {
  bits <1> Ii;
  let Inst{5-5} = Ii{0-0};
  bits <5> Vuu32;
  let Inst{12-8} = Vuu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vxx32;
  let Inst{4-0} = Vxx32{4-0};
}
class Enc_51635c : OpcodeHexagon {
  bits <7> Ii;
  let Inst{8-4} = Ii{6-2};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_e26546 : OpcodeHexagon {
  bits <5> Ii;
  let Inst{6-3} = Ii{4-1};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_70fb07 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-8} = Ii{5-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rxx32;
  let Inst{4-0} = Rxx32{4-0};
}
class Enc_6c9ee0 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_fa3ba4 : OpcodeHexagon {
  bits <14> Ii;
  let Inst{26-25} = Ii{13-12};
  let Inst{13-5} = Ii{11-3};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_44661f : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_277737 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{22-21} = Ii{7-6};
  let Inst{13-13} = Ii{5-5};
  let Inst{7-5} = Ii{4-2};
  bits <5> Ru32;
  let Inst{4-0} = Ru32{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{12-8} = Rd32{4-0};
}
class Enc_5c124a : OpcodeHexagon {
  bits <19> Ii;
  let Inst{26-25} = Ii{18-17};
  let Inst{20-16} = Ii{16-12};
  let Inst{13-13} = Ii{11-11};
  let Inst{7-0} = Ii{10-3};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
}
class Enc_928ca1 : OpcodeHexagon {
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_da664b : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_47ee5e : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <2> Pv4;
  let Inst{6-5} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ru32;
  let Inst{12-8} = Ru32{4-0};
  bits <3> Nt8;
  let Inst{2-0} = Nt8{2-0};
}
class Enc_8bcba4 : OpcodeHexagon {
  bits <6> II;
  let Inst{5-0} = II{5-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Re32;
  let Inst{20-16} = Re32{4-0};
}
class Enc_3a2484 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <4> n1;
  let Inst{28-28} = n1{3-3};
  let Inst{24-23} = n1{2-1};
  let Inst{13-13} = n1{0-0};
}
class Enc_a5ed8a : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_cb9321 : OpcodeHexagon {
  bits <16> Ii;
  let Inst{27-21} = Ii{15-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_668704 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <5> n1;
  let Inst{28-28} = n1{4-4};
  let Inst{25-22} = n1{3-0};
}
class Enc_a7341a : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_5eac98 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-8} = Ii{5-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_02553a : OpcodeHexagon {
  bits <7> Ii;
  let Inst{11-5} = Ii{6-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_acd6ed : OpcodeHexagon {
  bits <9> Ii;
  let Inst{10-5} = Ii{8-3};
  bits <2> Pt4;
  let Inst{12-11} = Pt4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_8e583a : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <5> n1;
  let Inst{28-28} = n1{4-4};
  let Inst{25-23} = n1{3-1};
  let Inst{13-13} = n1{0-0};
}
class Enc_b886fd : OpcodeHexagon {
  bits <5> Ii;
  let Inst{6-3} = Ii{4-1};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_24a7dc : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{23-19} = Vv32{4-0};
  bits <3> Rt8;
  let Inst{18-16} = Rt8{2-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_2d829e : OpcodeHexagon {
  bits <14> Ii;
  let Inst{10-0} = Ii{13-3};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_4f4ed7 : OpcodeHexagon {
  bits <18> Ii;
  let Inst{26-25} = Ii{17-16};
  let Inst{20-16} = Ii{15-11};
  let Inst{13-5} = Ii{10-2};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_84b2cd : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-7} = Ii{7-2};
  bits <5> II;
  let Inst{4-0} = II{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_8dbdfe : OpcodeHexagon {
  bits <8> Ii;
  let Inst{13-13} = Ii{7-7};
  let Inst{7-3} = Ii{6-2};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_90cd8b : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_bd0b33 : OpcodeHexagon {
  bits <10> Ii;
  let Inst{21-21} = Ii{9-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_8b8927 : OpcodeHexagon {
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vv32;
  let Inst{4-0} = Vv32{4-0};
}
class Enc_c7cd90 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{6-3} = Ii{3-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_405228 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <3> n1;
  let Inst{28-28} = n1{2-2};
  let Inst{24-23} = n1{1-0};
}
class Enc_81ac1d : OpcodeHexagon {
  bits <24> Ii;
  let Inst{24-16} = Ii{23-15};
  let Inst{13-1} = Ii{14-2};
}
class Enc_395cc4 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{6-3} = Ii{6-3};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_a51a9a : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-8} = Ii{7-3};
  let Inst{4-2} = Ii{2-0};
}
class Enc_d44e31 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{12-7} = Ii{5-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{4-0} = Rt32{4-0};
}
class Enc_f77fbc : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <3> Os8;
  let Inst{2-0} = Os8{2-0};
}
class Enc_d2216a : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_85bf58 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{6-3} = Ii{6-3};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_71bb9b : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_52a5dd : OpcodeHexagon {
  bits <4> Ii;
  let Inst{6-3} = Ii{3-0};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_5e2823 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_28a2dc : OpcodeHexagon {
  bits <5> Ii;
  let Inst{12-8} = Ii{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rx32;
  let Inst{4-0} = Rx32{4-0};
}
class Enc_5138b3 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_84d359 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{3-0} = Ii{3-0};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
}
class Enc_e07374 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_e0820b : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <2> Qs4;
  let Inst{6-5} = Qs4{1-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_323f2d : OpcodeHexagon {
  bits <6> II;
  let Inst{11-8} = II{5-2};
  let Inst{6-5} = II{1-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Re32;
  let Inst{20-16} = Re32{4-0};
}
class Enc_1a9974 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <2> Pv4;
  let Inst{6-5} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ru32;
  let Inst{12-8} = Ru32{4-0};
  bits <5> Rtt32;
  let Inst{4-0} = Rtt32{4-0};
}
class Enc_5de85f : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
}
class Enc_dd766a : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
class Enc_0b51ce : OpcodeHexagon {
  bits <3> Ii;
  let Inst{10-8} = Ii{2-0};
  bits <2> Qv4;
  let Inst{12-11} = Qv4{1-0};
  bits <5> Vs32;
  let Inst{4-0} = Vs32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_b4e6cf : OpcodeHexagon {
  bits <10> Ii;
  let Inst{21-21} = Ii{9-9};
  let Inst{13-5} = Ii{8-0};
  bits <5> Ru32;
  let Inst{4-0} = Ru32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_44215c : OpcodeHexagon {
  bits <6> Ii;
  let Inst{17-16} = Ii{5-4};
  let Inst{6-3} = Ii{3-0};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_0aa344 : OpcodeHexagon {
  bits <5> Gss32;
  let Inst{20-16} = Gss32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_a21d47 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{10-5} = Ii{5-0};
  bits <2> Pt4;
  let Inst{12-11} = Pt4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_cc449f : OpcodeHexagon {
  bits <4> Ii;
  let Inst{6-3} = Ii{3-0};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_645d54 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{5-5} = Ii{0-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_667b39 : OpcodeHexagon {
  bits <5> Css32;
  let Inst{20-16} = Css32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_927852 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_163a3c : OpcodeHexagon {
  bits <7> Ii;
  let Inst{12-7} = Ii{6-1};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{4-0} = Rt32{4-0};
}
class Enc_a75aa6 : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
}
class Enc_b087ac : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_691712 : OpcodeHexagon {
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_b1e1fb : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <5> n1;
  let Inst{28-28} = n1{4-4};
  let Inst{25-23} = n1{3-1};
  let Inst{8-8} = n1{0-0};
}
class Enc_1f19b5 : OpcodeHexagon {
  bits <5> Ii;
  let Inst{9-5} = Ii{4-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_b8c967 : OpcodeHexagon {
  bits <8> Ii;
  let Inst{12-5} = Ii{7-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_fb6577 : OpcodeHexagon {
  bits <2> Pu4;
  let Inst{9-8} = Pu4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_2bae10 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{10-8} = Ii{3-1};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_c4dc92 : OpcodeHexagon {
  bits <2> Qv4;
  let Inst{23-22} = Qv4{1-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_03833b : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_dbd70c : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <2> Pu4;
  let Inst{6-5} = Pu4{1-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_f6fe0b : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <6> n1;
  let Inst{28-28} = n1{5-5};
  let Inst{24-22} = n1{4-2};
  let Inst{13-13} = n1{1-1};
  let Inst{8-8} = n1{0-0};
}
class Enc_9e2e1c : OpcodeHexagon {
  bits <5> Ii;
  let Inst{8-5} = Ii{4-1};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Ryy32;
  let Inst{4-0} = Ryy32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_8df4be : OpcodeHexagon {
  bits <17> Ii;
  let Inst{26-25} = Ii{16-15};
  let Inst{20-16} = Ii{14-10};
  let Inst{13-5} = Ii{9-1};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_66bce1 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <4> Rd16;
  let Inst{11-8} = Rd16{3-0};
}
class Enc_b8309d : OpcodeHexagon {
  bits <9> Ii;
  let Inst{8-3} = Ii{8-3};
  bits <3> Rtt8;
  let Inst{2-0} = Rtt8{2-0};
}
class Enc_5e8512 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vxx32;
  let Inst{4-0} = Vxx32{4-0};
}
class Enc_4f677b : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <6> II;
  let Inst{11-8} = II{5-2};
  let Inst{6-5} = II{1-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_3d920a : OpcodeHexagon {
  bits <6> Ii;
  let Inst{8-5} = Ii{5-2};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_e83554 : OpcodeHexagon {
  bits <5> Ii;
  let Inst{8-5} = Ii{4-1};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_ed48be : OpcodeHexagon {
  bits <2> Ii;
  let Inst{6-5} = Ii{1-0};
  bits <3> Rdd8;
  let Inst{2-0} = Rdd8{2-0};
}
class Enc_f8c1c4 : OpcodeHexagon {
  bits <2> Pv4;
  let Inst{12-11} = Pv4{1-0};
  bits <1> Mu2;
  let Inst{13-13} = Mu2{0-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_1aa186 : OpcodeHexagon {
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rxx32;
  let Inst{4-0} = Rxx32{4-0};
}
class Enc_134437 : OpcodeHexagon {
  bits <2> Qs4;
  let Inst{9-8} = Qs4{1-0};
  bits <2> Qt4;
  let Inst{23-22} = Qt4{1-0};
  bits <2> Qd4;
  let Inst{1-0} = Qd4{1-0};
}
class Enc_f3f408 : OpcodeHexagon {
  bits <4> Ii;
  let Inst{13-13} = Ii{3-3};
  let Inst{10-8} = Ii{2-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vd32;
  let Inst{4-0} = Vd32{4-0};
}
class Enc_97d666 : OpcodeHexagon {
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rd16;
  let Inst{3-0} = Rd16{3-0};
}
class Enc_f82eaf : OpcodeHexagon {
  bits <8> Ii;
  let Inst{10-5} = Ii{7-2};
  bits <2> Pt4;
  let Inst{12-11} = Pt4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_69d63b : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
}
class Enc_f79415 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{6-6} = Ii{0-0};
  bits <6> II;
  let Inst{5-0} = II{5-0};
  bits <5> Ru32;
  let Inst{20-16} = Ru32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
}
class Enc_ce6828 : OpcodeHexagon {
  bits <14> Ii;
  let Inst{26-25} = Ii{13-12};
  let Inst{13-13} = Ii{11-11};
  let Inst{7-0} = Ii{10-3};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
}
class Enc_800e04 : OpcodeHexagon {
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <4> Rs16;
  let Inst{19-16} = Rs16{3-0};
  bits <6> n1;
  let Inst{28-28} = n1{5-5};
  let Inst{25-22} = n1{4-1};
  let Inst{13-13} = n1{0-0};
}
class Enc_ad1831 : OpcodeHexagon {
  bits <16> Ii;
  let Inst{26-25} = Ii{15-14};
  let Inst{20-16} = Ii{13-9};
  let Inst{13-13} = Ii{8-8};
  let Inst{7-0} = Ii{7-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_0fa531 : OpcodeHexagon {
  bits <15> Ii;
  let Inst{21-21} = Ii{14-14};
  let Inst{13-13} = Ii{13-13};
  let Inst{11-1} = Ii{12-2};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_7eaeb6 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{6-3} = Ii{5-2};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rx32;
  let Inst{20-16} = Rx32{4-0};
}
class Enc_f55a0c : OpcodeHexagon {
  bits <6> Ii;
  let Inst{11-8} = Ii{5-2};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
  bits <4> Rt16;
  let Inst{3-0} = Rt16{3-0};
}
class Enc_f20719 : OpcodeHexagon {
  bits <7> Ii;
  let Inst{12-7} = Ii{6-1};
  bits <6> II;
  let Inst{13-13} = II{5-5};
  let Inst{4-0} = II{4-0};
  bits <2> Pv4;
  let Inst{6-5} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_eafd18 : OpcodeHexagon {
  bits <5> II;
  let Inst{12-8} = II{4-0};
  bits <11> Ii;
  let Inst{21-20} = Ii{10-9};
  let Inst{7-1} = Ii{8-2};
  bits <3> Ns8;
  let Inst{18-16} = Ns8{2-0};
}
class Enc_7b523d : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{23-19} = Vv32{4-0};
  bits <3> Rt8;
  let Inst{18-16} = Rt8{2-0};
  bits <5> Vxx32;
  let Inst{4-0} = Vxx32{4-0};
}
class Enc_47ef61 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{7-5} = Ii{2-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rd32;
  let Inst{4-0} = Rd32{4-0};
}
class Enc_cc857d : OpcodeHexagon {
  bits <5> Vuu32;
  let Inst{12-8} = Vuu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <5> Vx32;
  let Inst{4-0} = Vx32{4-0};
}
class Enc_7fa7f6 : OpcodeHexagon {
  bits <6> II;
  let Inst{11-8} = II{5-2};
  let Inst{6-5} = II{1-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
  bits <5> Re32;
  let Inst{20-16} = Re32{4-0};
}
class Enc_0f8bab : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Rt32;
  let Inst{20-16} = Rt32{4-0};
  bits <2> Qd4;
  let Inst{1-0} = Qd4{1-0};
}
class Enc_7eb485 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{6-6} = Ii{0-0};
  bits <6> II;
  let Inst{5-0} = II{5-0};
  bits <5> Ru32;
  let Inst{20-16} = Ru32{4-0};
  bits <3> Nt8;
  let Inst{10-8} = Nt8{2-0};
}
class Enc_864a5a : OpcodeHexagon {
  bits <9> Ii;
  let Inst{12-8} = Ii{8-4};
  let Inst{4-3} = Ii{3-2};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
}
class Enc_c2b48e : OpcodeHexagon {
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
  bits <2> Pd4;
  let Inst{1-0} = Pd4{1-0};
}
class Enc_8c6530 : OpcodeHexagon {
  bits <5> Rtt32;
  let Inst{12-8} = Rtt32{4-0};
  bits <5> Rss32;
  let Inst{20-16} = Rss32{4-0};
  bits <2> Pu4;
  let Inst{6-5} = Pu4{1-0};
  bits <5> Rdd32;
  let Inst{4-0} = Rdd32{4-0};
}
class Enc_448f7f : OpcodeHexagon {
  bits <11> Ii;
  let Inst{26-25} = Ii{10-9};
  let Inst{13-13} = Ii{8-8};
  let Inst{7-0} = Ii{7-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_da8d43 : OpcodeHexagon {
  bits <6> Ii;
  let Inst{13-13} = Ii{5-5};
  let Inst{7-3} = Ii{4-0};
  bits <2> Pv4;
  let Inst{1-0} = Pv4{1-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Rt32;
  let Inst{12-8} = Rt32{4-0};
}
class Enc_a6ce9c : OpcodeHexagon {
  bits <6> Ii;
  let Inst{3-0} = Ii{5-2};
  bits <4> Rs16;
  let Inst{7-4} = Rs16{3-0};
}
class Enc_3b7631 : OpcodeHexagon {
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vdddd32;
  let Inst{4-0} = Vdddd32{4-0};
  bits <3> Rx8;
  let Inst{18-16} = Rx8{2-0};
}
class Enc_eca7c8 : OpcodeHexagon {
  bits <2> Ii;
  let Inst{13-13} = Ii{1-1};
  let Inst{7-7} = Ii{0-0};
  bits <5> Rs32;
  let Inst{20-16} = Rs32{4-0};
  bits <5> Ru32;
  let Inst{12-8} = Ru32{4-0};
  bits <5> Rt32;
  let Inst{4-0} = Rt32{4-0};
}
class Enc_4b39e4 : OpcodeHexagon {
  bits <3> Ii;
  let Inst{7-5} = Ii{2-0};
  bits <5> Vu32;
  let Inst{12-8} = Vu32{4-0};
  bits <5> Vv32;
  let Inst{20-16} = Vv32{4-0};
  bits <5> Vdd32;
  let Inst{4-0} = Vdd32{4-0};
}
