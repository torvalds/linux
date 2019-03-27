/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option destructive
#pragma D option quiet

BEGIN
/0/
{
	@agg996[996] = quantize(998);
	@agg997[997] = count();
	@agg998[998] = min(998);
	@agg999[999] = lquantize(0, -10, 10, 1);
}

BEGIN
{
	@agg0[0] = sum(0);
	@agg1[1] = sum(1);
	@agg2[2] = sum(2);
	@agg3[3] = sum(3);
	@agg4[4] = sum(4);
	@agg5[5] = sum(5);
	@agg6[6] = sum(6);
	@agg7[7] = sum(7);
	@agg8[8] = sum(8);
	@agg9[9] = sum(9);
	@agg10[10] = sum(10);
	@agg11[11] = sum(11);
	@agg12[12] = sum(12);
	@agg13[13] = sum(13);
	@agg14[14] = sum(14);
	@agg15[15] = sum(15);
	@agg16[16] = sum(16);
	@agg17[17] = sum(17);
	@agg18[18] = sum(18);
	@agg19[19] = sum(19);
	@agg20[20] = sum(20);
	@agg21[21] = sum(21);
	@agg22[22] = sum(22);
	@agg23[23] = sum(23);
	@agg24[24] = sum(24);
	@agg25[25] = sum(25);
	@agg26[26] = sum(26);
	@agg27[27] = sum(27);
	@agg28[28] = sum(28);
	@agg29[29] = sum(29);
	@agg30[30] = sum(30);
	@agg31[31] = sum(31);
	@agg32[32] = sum(32);
	@agg33[33] = sum(33);
	@agg34[34] = sum(34);
	@agg35[35] = sum(35);
	@agg36[36] = sum(36);
	@agg37[37] = sum(37);
	@agg38[38] = sum(38);
	@agg39[39] = sum(39);
	@agg40[40] = sum(40);
	@agg41[41] = sum(41);
	@agg42[42] = sum(42);
	@agg43[43] = sum(43);
	@agg44[44] = sum(44);
	@agg45[45] = sum(45);
	@agg46[46] = sum(46);
	@agg47[47] = sum(47);
	@agg48[48] = sum(48);
	@agg49[49] = sum(49);
	@agg50[50] = sum(50);
	@agg51[51] = sum(51);
	@agg52[52] = sum(52);
	@agg53[53] = sum(53);
	@agg54[54] = sum(54);
	@agg55[55] = sum(55);
	@agg56[56] = sum(56);
	@agg57[57] = sum(57);
	@agg58[58] = sum(58);
	@agg59[59] = sum(59);
	@agg60[60] = sum(60);
	@agg61[61] = sum(61);
	@agg62[62] = sum(62);
	@agg63[63] = sum(63);
	@agg64[64] = sum(64);
	@agg65[65] = sum(65);
	@agg66[66] = sum(66);
	@agg67[67] = sum(67);
	@agg68[68] = sum(68);
	@agg69[69] = sum(69);
	@agg70[70] = sum(70);
	@agg71[71] = sum(71);
	@agg72[72] = sum(72);
	@agg73[73] = sum(73);
	@agg74[74] = sum(74);
	@agg75[75] = sum(75);
	@agg76[76] = sum(76);
	@agg77[77] = sum(77);
	@agg78[78] = sum(78);
	@agg79[79] = sum(79);
	@agg80[80] = sum(80);
	@agg81[81] = sum(81);
	@agg82[82] = sum(82);
	@agg83[83] = sum(83);
	@agg84[84] = sum(84);
	@agg85[85] = sum(85);
	@agg86[86] = sum(86);
	@agg87[87] = sum(87);
	@agg88[88] = sum(88);
	@agg89[89] = sum(89);
	@agg90[90] = sum(90);
	@agg91[91] = sum(91);
	@agg92[92] = sum(92);
	@agg93[93] = sum(93);
	@agg94[94] = sum(94);
	@agg95[95] = sum(95);
	@agg96[96] = sum(96);
	@agg97[97] = sum(97);
	@agg98[98] = sum(98);
	@agg99[99] = sum(99);
	@agg100[100] = sum(100);
	@agg101[101] = sum(101);
	@agg102[102] = sum(102);
	@agg103[103] = sum(103);
	@agg104[104] = sum(104);
	@agg105[105] = sum(105);
	@agg106[106] = sum(106);
	@agg107[107] = sum(107);
	@agg108[108] = sum(108);
	@agg109[109] = sum(109);
	@agg110[110] = sum(110);
	@agg111[111] = sum(111);
	@agg112[112] = sum(112);
	@agg113[113] = sum(113);
	@agg114[114] = sum(114);
	@agg115[115] = sum(115);
	@agg116[116] = sum(116);
	@agg117[117] = sum(117);
	@agg118[118] = sum(118);
	@agg119[119] = sum(119);
	@agg120[120] = sum(120);
	@agg121[121] = sum(121);
	@agg122[122] = sum(122);
	@agg123[123] = sum(123);
	@agg124[124] = sum(124);
	@agg125[125] = sum(125);
	@agg126[126] = sum(126);
	@agg127[127] = sum(127);
	@agg128[128] = sum(128);
	@agg129[129] = sum(129);
	@agg130[130] = sum(130);
	@agg131[131] = sum(131);
	@agg132[132] = sum(132);
	@agg133[133] = sum(133);
	@agg134[134] = sum(134);
	@agg135[135] = sum(135);
	@agg136[136] = sum(136);
	@agg137[137] = sum(137);
	@agg138[138] = sum(138);
	@agg139[139] = sum(139);
	@agg140[140] = sum(140);
	@agg141[141] = sum(141);
	@agg142[142] = sum(142);
	@agg143[143] = sum(143);
	@agg144[144] = sum(144);
	@agg145[145] = sum(145);
	@agg146[146] = sum(146);
	@agg147[147] = sum(147);
	@agg148[148] = sum(148);
	@agg149[149] = sum(149);
	@agg150[150] = sum(150);
	@agg151[151] = sum(151);
	@agg152[152] = sum(152);
	@agg153[153] = sum(153);
	@agg154[154] = sum(154);
	@agg155[155] = sum(155);
	@agg156[156] = sum(156);
	@agg157[157] = sum(157);
	@agg158[158] = sum(158);
	@agg159[159] = sum(159);
	@agg160[160] = sum(160);
	@agg161[161] = sum(161);
	@agg162[162] = sum(162);
	@agg163[163] = sum(163);
	@agg164[164] = sum(164);
	@agg165[165] = sum(165);
	@agg166[166] = sum(166);
	@agg167[167] = sum(167);
	@agg168[168] = sum(168);
	@agg169[169] = sum(169);
	@agg170[170] = sum(170);
	@agg171[171] = sum(171);
	@agg172[172] = sum(172);
	@agg173[173] = sum(173);
	@agg174[174] = sum(174);
	@agg175[175] = sum(175);
	@agg176[176] = sum(176);
	@agg177[177] = sum(177);
	@agg178[178] = sum(178);
	@agg179[179] = sum(179);
	@agg180[180] = sum(180);
	@agg181[181] = sum(181);
	@agg182[182] = sum(182);
	@agg183[183] = sum(183);
	@agg184[184] = sum(184);
	@agg185[185] = sum(185);
	@agg186[186] = sum(186);
	@agg187[187] = sum(187);
	@agg188[188] = sum(188);
	@agg189[189] = sum(189);
	@agg190[190] = sum(190);
	@agg191[191] = sum(191);
	@agg192[192] = sum(192);
	@agg193[193] = sum(193);
	@agg194[194] = sum(194);
	@agg195[195] = sum(195);
	@agg196[196] = sum(196);
	@agg197[197] = sum(197);
	@agg198[198] = sum(198);
	@agg199[199] = sum(199);
	@agg200[200] = sum(200);
	@agg201[201] = sum(201);
	@agg202[202] = sum(202);
	@agg203[203] = sum(203);
	@agg204[204] = sum(204);
	@agg205[205] = sum(205);
	@agg206[206] = sum(206);
	@agg207[207] = sum(207);
	@agg208[208] = sum(208);
	@agg209[209] = sum(209);
	@agg210[210] = sum(210);
	@agg211[211] = sum(211);
	@agg212[212] = sum(212);
	@agg213[213] = sum(213);
	@agg214[214] = sum(214);
	@agg215[215] = sum(215);
	@agg216[216] = sum(216);
	@agg217[217] = sum(217);
	@agg218[218] = sum(218);
	@agg219[219] = sum(219);
	@agg220[220] = sum(220);
	@agg221[221] = sum(221);
	@agg222[222] = sum(222);
	@agg223[223] = sum(223);
	@agg224[224] = sum(224);
	@agg225[225] = sum(225);
	@agg226[226] = sum(226);
	@agg227[227] = sum(227);
	@agg228[228] = sum(228);
	@agg229[229] = sum(229);
	@agg230[230] = sum(230);
	@agg231[231] = sum(231);
	@agg232[232] = sum(232);
	@agg233[233] = sum(233);
	@agg234[234] = sum(234);
	@agg235[235] = sum(235);
	@agg236[236] = sum(236);
	@agg237[237] = sum(237);
	@agg238[238] = sum(238);
	@agg239[239] = sum(239);
	@agg240[240] = sum(240);
	@agg241[241] = sum(241);
	@agg242[242] = sum(242);
	@agg243[243] = sum(243);
	@agg244[244] = sum(244);
	@agg245[245] = sum(245);
	@agg246[246] = sum(246);

	printa("%8d %8@d %8@d\n", @agg0, @agg1);
	printf("\n");

	printa("%8d %8@d %8@d\n", @agg0, @agg996);
	printf("\n");

	printa("%4d %4@d %4@d %4@d %4@d %4@d %4@d %4@d %4@d %4@d %4@d %4@d\n",
	    @agg12, @agg3, @agg73, @agg997,
	    @agg9, @agg9, @agg4, @agg998,
	    @agg11, @agg23, @agg69);

	printf("\n");

	printa("%8d %8@d %8@d\n", @agg245, @agg246);
	printf("\n");

	printa("%8d %8@d %8@d\n", @agg999, @agg246);
	printf("\n");

	exit(0);
}
