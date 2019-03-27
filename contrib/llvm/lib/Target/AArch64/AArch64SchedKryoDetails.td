//=- AArch64SchedKryoDetails.td - QC Kryo Scheduling Defs ----*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the uop and latency details for the machine model for the
// Qualcomm Kryo subtarget.
//
//===----------------------------------------------------------------------===//

def KryoWrite_3cyc_X_noRSV_138ln :
	SchedWriteRes<[KryoUnitX]> {
    let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_X_noRSV_138ln],
    (instregex "(S|U)R?SRA(d|(v2i32|v4i16|v8i8)_shift)")>;

def KryoWrite_3cyc_X_X_139ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
    let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_X_X_139ln],
    (instregex "(S|U)R?SRA(v2i64|v4i32|v8i16|v16i8)_shift")>;

def KryoWrite_4cyc_XY_XY_noRSV_172ln :
    SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
    let Latency = 4; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_4cyc_XY_XY_noRSV_172ln],
	(instregex "(S|U)ABA(v8i8|v4i16|v2i32)")>;
def KryoWrite_4cyc_XY_XY_XY_XY_178ln :
    SchedWriteRes<[KryoUnitXY, KryoUnitXY, KryoUnitXY, KryoUnitXY]> {
    let Latency = 4; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_4cyc_XY_XY_XY_XY_178ln],
	(instregex "(S|U)ABA(v16i8|v8i16|v4i32)")>;
def KryoWrite_3cyc_XY_XY_XY_XY_177ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY, KryoUnitXY, KryoUnitXY]> {
    let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_XY_XY_177ln],
	(instregex "(S|U)ABALv.*")>;
def KryoWrite_3cyc_XY_XY_166ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
    let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_166ln],
	(instregex "(S|U)(ABD|QSUB|RHADD)(v16i8|v8i16|v4i32|v2i64)")>;
def KryoWrite_3cyc_XY_noRSV_159ln :
	SchedWriteRes<[KryoUnitXY]> {
    let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_159ln],
	(instregex "(S|U)(ABD|RHADD)(v8i8|v4i16|v2i32)")>;
def KryoWrite_3cyc_XY_XY_165ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
    let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_165ln],
	(instregex "(S|U)ABDLv.*")>;
def KryoWrite_3cyc_X_noRSV_154ln :
	SchedWriteRes<[KryoUnitX]> {
let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_X_noRSV_154ln],
	(instregex "(S|U)ADALP(v8i8|v4i16|v2i32)_v.*")>;
def KryoWrite_3cyc_X_X_155ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_X_X_155ln],
	(instregex "(S|U)ADALP(v16i8|v8i16|v4i32)_v.*")>;
def KryoWrite_2cyc_XY_XY_151ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_151ln],
	(instregex "(S|U)(ADD|SUB)Lv.*")>;
def KryoWrite_2cyc_XY_noRSV_148ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_148ln],
	(instregex "((S|U)ADDLP|ABS)(v2i32|v4i16|v8i8)(_v.*)?")>;
def KryoWrite_2cyc_XY_XY_150ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_150ln],
	(instregex "((S|U)ADDLP|ABS)(v2i64|v4i32|v8i16|v16i8)(_v.*)?")>;
def KryoWrite_3cyc_XY_XY_XY_noRSV_179ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_XY_noRSV_179ln],
	(instrs SADDLVv4i32v, UADDLVv4i32v)>;
def KryoWrite_5cyc_XY_XY_XY_noRSV_180ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY, KryoUnitXY]> {
	let Latency = 5; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_5cyc_XY_XY_XY_noRSV_180ln],
	(instrs SADDLVv8i16v, UADDLVv8i16v)>;
def KryoWrite_6cyc_XY_XY_X_noRSV_181ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY, KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_6cyc_XY_XY_X_noRSV_181ln],
	(instrs SADDLVv16i8v, UADDLVv16i8v)>;
def KryoWrite_3cyc_XY_noRSV_158ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_158ln],
	(instrs SADDLVv4i16v, UADDLVv4i16v, ADDVv4i16v)>;
def KryoWrite_4cyc_X_noRSV_169ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_169ln],
	(instrs SADDLVv8i8v, UADDLVv8i8v, ADDVv8i8v)>;
def KryoWrite_2cyc_XY_XY_XY_XY_176ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY, KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_XY_XY_176ln],
	(instregex "(S|U)(ADDW|SUBW)v.*")>;
def KryoWrite_4cyc_X_noRSV_40ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_40ln],
	(instregex "(S|U)CVTFS(W|X)(D|S)ri")>;
def KryoWrite_4cyc_X_noRSV_97ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_97ln],
	(instregex "(S|U)CVTFU(W|X)(D|S)ri")>;
def KryoWrite_4cyc_X_noRSV_110ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_110ln],
	(instregex "(S|U)CVTF(v1i32|v2i32|v1i64|v2f32|d|s)(_shift)?")>;
def KryoWrite_4cyc_X_X_114ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_X_114ln],
	(instregex "(S|U)CVTF(v2i64|v4i32|v2f64|v4f32)(_shift)?")>;
def KryoWrite_1cyc_XA_Y_98ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XA_Y_98ln],
	(instregex "(S|U)DIV(_Int)?(W|X)r")>;
def KryoWrite_2cyc_XY_XY_152ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_152ln],
	(instregex "(S|U)H(ADD|SUB)(v16i8|v8i16|v4i32)")>;
def KryoWrite_2cyc_XY_noRSV_149ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_149ln],
	(instregex "((S|U)H(ADD|SUB)|ADDP)(v8i8|v4i16|v2i32)")>;
def KryoWrite_4cyc_X_70ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_X_70ln],
	(instregex "(S|U)(MADDL|MSUBL)rrr")>;
def KryoWrite_4cyc_X_X_191ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_X_191ln],
	(instregex "(S|U|SQD)(MLAL|MLSL|MULL)v.*")>;
def KryoWrite_1cyc_XY_195ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_195ln],
	(instregex "(S|U)MOVv.*")>;
def KryoWrite_5cyc_X_71ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_5cyc_X_71ln],
	(instrs SMULHrr, UMULHrr)>;
def KryoWrite_3cyc_XY_noRSV_186ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_186ln],
	(instregex "^(S|U)QADD(v8i8|v4i16|v2i32)")>;
def KryoWrite_3cyc_XY_XY_187ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_187ln],
	(instregex "^(S|U)QADD(v16i8|v8i16|v4i32|v2i64)")>;
def KryoWrite_3cyc_XY_noRSV_69ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_69ln],
	(instregex "(S|U|SU|US)QADD(v1i8|v1i16|v2i16|v1i32|v1i64)")>;
def KryoWrite_3cyc_XY_noRSV_248ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_248ln],
	(instregex "(S|U)QSHLU?(d|s|h|b|(v8i8|v4i16|v2i32)_shift)$")>;
def KryoWrite_3cyc_XY_XY_250ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_250ln],
	(instregex "(S|U)(QSHLU?|RSHR)(v16i8|v8i16|v4i32|v2i64)_shift$")>;
def KryoWrite_3cyc_XY_noRSV_246ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_246ln],
	(instregex "(S|U)(QSHL|RSHL|QRSHL)(v1i8|v1i16|v1i32|v1i64|v8i8|v4i16|v2i32)$")>;
def KryoWrite_3cyc_XY_XY_251ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_251ln],
	(instregex "(S|U)(QSHL|RSHL|QRSHL)(v16i8|v8i16|v4i32|v2i64)$")>;
def KryoWrite_6cyc_XY_X_238ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_XY_X_238ln],
	(instregex "((S|U)QR?SHRN|SQR?SHRUN)(v16i8|v8i16|v4i32)_shift$")>;
def KryoWrite_3cyc_XY_noRSV_249ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_249ln],
	(instregex "((S|U)QR?SHRN|SQR?SHRUN)(s|h|b)?")>;
def KryoWrite_6cyc_XY_X_noRSV_252ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_6cyc_XY_X_noRSV_252ln],
	(instregex "((S|U)QR?SHRN|SQR?SHRUN)(v8i8|v4i16|v2i32)_shift?")>;
def KryoWrite_3cyc_XY_noRSV_161ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_161ln],
	(instregex "(S|U)QSUB(v8i8|v4i16|v2i32|v1i64|v1i32|v1i16|v1i8)")>;
def KryoWrite_3cyc_XY_noRSV_163ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_163ln],
	(instregex "(S|U)QXTU?N(v16i8|v8i16|v4i32|v8i8|v4i16|v2i32)")>;
def KryoWrite_3cyc_XY_noRSV_162ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_162ln],
	(instregex "(S|U)QXTU?N(v1i8|v1i16|v1i32)")>;
def KryoWrite_3cyc_XY_noRSV_247ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_247ln],
	(instregex "(S|U)RSHR(d|(v8i8|v4i16|v2i32)_shift)$")>;
def KryoWrite_2cyc_XY_noRSV_239ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_239ln],
	(instregex "(S|U)SHL(d|v8i8|v4i16|v2i32|v1i64)$")>;
def KryoWrite_2cyc_XY_XY_243ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_243ln],
	(instregex "(S|U)SHL(v16i8|v8i16|v4i32|v2i64)$")>;
def KryoWrite_2cyc_XY_XY_241ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_241ln],
	(instregex "(S|U)?SHLL(v16i8|v8i16|v4i32|v8i8|v4i16|v2i32)(_shift)?$")>;
def KryoWrite_2cyc_XY_noRSV_240ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_240ln],
	(instregex "((S|U)SHR|SHL)(d|(v8i8|v4i16|v2i32)_shift)$")>;
def KryoWrite_2cyc_XY_XY_242ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_242ln],
	(instregex "((S|U)SHR|SHL)(v16i8|v8i16|v4i32|v2i64)_shift$")>;
def KryoWrite_2cyc_XY_XY_183ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_183ln],
	(instregex "(S|U)(MAX|MIN)P?(v16i8|v8i16|v4i32)")>;
def KryoWrite_2cyc_XY_noRSV_182ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_182ln],
	(instregex "(S|U)(MAX|MIN)P?(v8i8|v4i16|v2i32)")>;
def KryoWrite_3cyc_XY_noRSV_184ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_184ln],
	(instregex "(S|U)(MAX|MIN)V(v4i16v|v8i8v|v4i32)")>;
def KryoWrite_4cyc_X_noRSV_185ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_185ln],
	(instregex "(S|U)(MAX|MIN)V(v16i8v|v8i16v)")>;
def KryoWrite_2cyc_XY_noRSV_67ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_67ln],
	(instrs ABSv1i64)>;
def KryoWrite_1cyc_XY_63ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_63ln, ReadI, ReadI],
	(instregex "ADC.*")>;
def KryoWrite_1cyc_XY_63_1ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_63_1ln],
	(instregex "ADR.*")>;
def KryoWrite_1cyc_XY_62ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_62ln, ReadI],
	(instregex "ADDS?(W|X)ri")>;
def KryoWrite_2cyc_XY_XY_64ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_64ln, ReadI, ReadI],
	(instregex "ADDS?(W|X)r(r|s|x)(64)?")>;
def KryoWrite_1cyc_XY_noRSV_65ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_65ln],
	(instrs ADDv1i64)>;
def KryoWrite_1cyc_XY_noRSV_144ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_144ln],
	(instregex "(ADD|SUB)(v8i8|v4i16|v2i32|v1i64)")>;
def KryoWrite_1cyc_XY_XY_146ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_146ln],
	(instregex "(ADD|SUB)(v16i8|v8i16|v4i32|v2i64)")>;
def KryoWrite_4cyc_XY_X_noRSV_171ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_4cyc_XY_X_noRSV_171ln],
	(instregex "(ADD|SUB)HNv.*")>;
def KryoWrite_1cyc_XY_noRSV_66ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_66ln],
	(instrs ADDPv2i64p)>;
def KryoWrite_2cyc_XY_XY_153ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_153ln],
	(instregex "ADDP(v16i8|v8i16|v4i32|v2i64)")>;
def KryoWrite_3cyc_XY_XY_noRSV_170ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_noRSV_170ln],
	(instrs ADDVv4i32v)>;
def KryoWrite_4cyc_XY_XY_noRSV_173ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 4; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_4cyc_XY_XY_noRSV_173ln],
	(instrs ADDVv8i16v)>;
def KryoWrite_5cyc_XY_X_noRSV_174ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_5cyc_XY_X_noRSV_174ln],
	(instrs ADDVv16i8v)>;
def KryoWrite_3cyc_XY_XY_X_X_27ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY, KryoUnitX, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_X_X_27ln],
	(instrs AESDrr, AESErr)>;
def KryoWrite_2cyc_X_X_22ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_X_X_22ln],
	(instrs AESIMCrr, AESMCrr)>;
def KryoWrite_1cyc_XY_noRSV_76ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_76ln],
	(instregex "((AND|ORN|EOR|EON)S?(Wr[rsi]|v8i8|v4i16|v2i32)|(ORR|BIC)S?(Wr[rs]|v8i8|v4i16|v2i32))")>;
def KryoWrite_1cyc_XY_XY_79ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_79ln],
	(instregex "((AND|ORN|EOR|EON)S?(Xr[rsi]|v16i8|v8i16|v4i32)|(ORR|BIC)S?(Xr[rs]|v16i8|v8i16|v4i32))")>;
def KryoWrite_1cyc_X_72ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_X_72ln],
	(instregex "(S|U)?BFM.*")>;
def KryoWrite_1cyc_XY_noRSV_77ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_77ln],
	(instregex "(BIC|ORR)S?Wri")>;
def KryoWrite_1cyc_XY_XY_78ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_78ln],
	(instregex "(BIC|ORR)S?Xri")>;
def KryoWrite_1cyc_X_noRSV_74ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_noRSV_74ln],
	(instrs BIFv8i8, BITv8i8, BSLv8i8)>;
def KryoWrite_1cyc_X_X_75ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_X_75ln],
	(instrs BIFv16i8, BITv16i8, BSLv16i8)>;
def KryoWrite_0cyc_noRSV_11ln :
	SchedWriteRes<[]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_noRSV_11ln],
	(instrs BRK, DCPS1, DCPS2, DCPS3, HLT, HVC, ISB, HINT, SMC, SVC)>;
def KryoWrite_0cyc_XY_16ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_XY_16ln, ReadI],
	(instregex "(CCMN|CCMP)(W|X)i")>;
def KryoWrite_0cyc_XY_16_1ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_XY_16_1ln, ReadI, ReadI],
	(instregex "(CCMN|CCMP)(W|X)r")>;
def KryoWrite_2cyc_XY_3ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_2cyc_XY_3ln, ReadI],
	(instregex "(CLS|CLZ)(W|X)r")>;
def KryoWrite_2cyc_XY_noRSV_7ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_7ln],
	(instregex "(CLS|CLZ|CNT)(v4i32|v8i16|v16i8)")>;
def KryoWrite_2cyc_XY_XY_8ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_8ln],
	(instregex "(CLS|CLZ|CNT)(v2i32|v4i16|v8i8)")>;
def KryoWrite_2cyc_XY_noRSV_80ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_80ln],
	(instregex "CM(EQ|GE|HS|GT|HI|TST)(v8i8|v4i16|v2i32|v1i64)$")>;
def KryoWrite_2cyc_XY_XY_83ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_83ln],
	(instregex "CM(EQ|GE|HS|GT|HI|TST)(v16i8|v8i16|v4i32|v2i64)$")>;
def KryoWrite_2cyc_XY_noRSV_81ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_81ln],
	(instregex "CM(EQ|LE|GE|GT|LT)(v8i8|v4i16|v2i32|v1i64)rz$")>;
def KryoWrite_2cyc_XY_XY_82ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_82ln],
	(instregex "CM(EQ|LE|GE|GT|LT)(v16i8|v8i16|v4i32|v2i64)rz$")>;
def KryoWrite_3cyc_XY_4ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_XY_4ln, ReadI, ReadISReg],
	(instregex "CRC32.*")>;
def KryoWrite_1cyc_XY_20ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_20ln, ReadI, ReadI],
	(instregex "CSEL(W|X)r")>;
def KryoWrite_1cyc_X_17ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_X_17ln, ReadI, ReadI],
	(instregex "(CSINC|CSNEG)(W|X)r")>;
def KryoWrite_1cyc_XY_18ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_18ln, ReadI, ReadI],
	(instregex "(CSINV)(W|X)r")>;
def KryoWrite_3cyc_LS_X_13ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_X_13ln],
	(instrs DRPS)>;
def KryoWrite_0cyc_LS_10ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_LS_10ln],
	(instrs DSB, DMB, CLREX)>;
def KryoWrite_1cyc_X_noRSV_196ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_noRSV_196ln],
	(instregex "DUP(v8i8|v4i16|v2i32)(gpr|lane)")>;
def KryoWrite_1cyc_X_X_197ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_X_197ln],
	(instregex "DUP(v16i8|v8i16|v4i32|v2i64)(gpr|lane)")>;
def KryoWrite_3cyc_LS_LS_X_15ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_X_15ln],
	(instrs ERET)>;
def KryoWrite_1cyc_X_noRSV_207ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_noRSV_207ln],
	(instrs EXTv8i8)>;
def KryoWrite_1cyc_X_X_212ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_X_212ln],
	(instrs EXTv16i8)>;
def KryoWrite_2cyc_XY_X_136ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitX]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_X_136ln],
	(instrs EXTRWrri, EXTRXrri)>;
def KryoWrite_2cyc_XY_noRSV_35ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_35ln],
	(instregex "F(MAX|MIN)(NM)?P?(D|S)rr")>;
def KryoWrite_2cyc_XY_XY_106ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_106ln],
	(instregex "(F(MAX|MIN)(NM)?P?|FAC(GE|GT)|FCM(EQ|GE|GT))(v2i64p|v2f64|v4f32)")>;
def KryoWrite_2cyc_XY_noRSV_104ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_104ln],
	(instregex "(F(MAX|MIN)(NM)?P?|FAC(GE|GT)|FCM(EQ|GE|GT))(v2f32|v2i32p)")>;
def KryoWrite_3cyc_XY_noRSV_107ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_107ln],
	(instregex "F(MAX|MIN)(NM)?Vv4i32v")>;
def KryoWrite_3cyc_XY_noRSV_101ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_101ln],
	(instregex "FABD(32|64|v2f32)")>;
def KryoWrite_3cyc_XY_XY_103ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_103ln],
	(instregex "(FABD|FADD|FSUB|FADDP)(v4f32|v2f64)")>;
def KryoWrite_1cyc_XY_noRSV_48ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_48ln],
	(instregex "F(ABS|NEG)(D|S)r")>;
def KryoWrite_1cyc_XY_noRSV_124ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_124ln],
	(instregex "F(ABS|NEG)v2f32")>;
def KryoWrite_1cyc_XY_XY_125ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_125ln],
	(instregex "F(ABS|NEG)(v2f64|v4f32)")>;
def KryoWrite_2cyc_XY_noRSV_33ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_33ln],
	(instregex "(FAC(GE|GT)|FCM(EQ|GE|GT))(32|64)")>;
def KryoWrite_3cyc_XY_noRSV_30ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_30ln],
	(instregex "(FADD|FSUB)(D|S)rr")>;
def KryoWrite_3cyc_XY_noRSV_100ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_100ln],
	(instregex "(FADD|FSUB|FADDP)v2f32")>;
def KryoWrite_3cyc_XY_noRSV_29ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_29ln],
	(instregex "FADDP(v2i32p|v2i64p)")>;
def KryoWrite_0cyc_XY_31ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_XY_31ln],
	(instregex "FCCMPE?(D|S)rr")>;
def KryoWrite_2cyc_XY_noRSV_34ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_34ln],
	(instregex "FCM(EQ|LE|GE|GT|LT)(v1i32|v1i64)rz")>;
def KryoWrite_2cyc_XY_XY_36ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_36ln],
	(instregex "FCM(EQ|LE|GE|GT|LT)(v2i64|v4i32)rz")>;
def KryoWrite_2cyc_XY_noRSV_105ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_105ln],
	(instregex "FCM(EQ|LE|GE|GT|LT)v2i32rz")>;
def KryoWrite_0cyc_XY_32ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_XY_32ln],
	(instregex "FCMPE?(D|S)r(r|i)")>;
def KryoWrite_1cyc_XY_noRSV_49ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_49ln],
	(instrs FCSELDrrr, FCSELSrrr)>;
def KryoWrite_4cyc_X_noRSV_41ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_41ln],
	(instrs FCVTDHr, FCVTDSr, FCVTHDr, FCVTHSr, FCVTSDr, FCVTSHr)>;
def KryoWrite_4cyc_X_38ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_X_38ln],
	(instregex "FCVT(((A|N|M|P)(S|U)(S|U)|Z(S|U)_Int(S|U))(W|X)(D|S)ri?|Z(S|U)(d|s))$")>;
def KryoWrite_4cyc_X_noRSV_113ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_113ln],
	(instregex "FCVT((A|N|M|P)(S|U)|Z(S|U)_Int)(v1i32|v1i64|v2f32)$")>;
def KryoWrite_4cyc_X_X_117ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_X_117ln],
	(instregex "FCVT((A|N|M|P)(S|U)|Z(S|U)_Int)(v4f32|v2f64)$")>;
def KryoWrite_5cyc_X_X_XY_noRSV_119ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitXY]> {
	let Latency = 5; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_5cyc_X_X_XY_noRSV_119ln],
	(instregex "FCVTX?N(v2f32|v4f32|v2i32|v4i16|v4i32|v8i16)$")>;
def KryoWrite_4cyc_X_X_116ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_X_116ln],
	(instregex "FCVTL(v2i32|v4i16|v4i32|v8i16)$")>;
def KryoWrite_4cyc_X_noRSV_112ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_112ln],
	(instrs FCVTXNv1i64)>;
def KryoWrite_4cyc_X_37ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_X_37ln],
	(instregex "FCVTZ(S|U)(S|U)(W|X)(D|S)ri?$")>;
def KryoWrite_4cyc_X_noRSV_111ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_111ln],
	(instregex "FCVTZ(S|U)(v2f32|v1i32|v1i64|v2i32(_shift)?)$")>;
def KryoWrite_4cyc_X_X_115ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_X_115ln],
	(instregex "FCVTZ(S|U)(v2f64|v4f32|(v2i64|v4i32)(_shift)?)$")>;
def KryoWrite_10cyc_XA_Y_noRSV_43ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY]> {
	let Latency = 10; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_10cyc_XA_Y_noRSV_43ln],
	(instrs FDIVSrr)>;
def KryoWrite_14cyc_XA_Y_noRSV_43ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY]> {
	let Latency = 14; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_14cyc_XA_Y_noRSV_43ln],
	(instrs FDIVDrr)>;
def KryoWrite_10cyc_XA_Y_noRSV_121ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY]> {
	let Latency = 10; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_10cyc_XA_Y_noRSV_121ln],
	(instrs FDIVv2f32)>;
def KryoWrite_14cyc_XA_Y_XA_Y_123ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY, KryoUnitXA, KryoUnitY]> {
	let Latency = 14; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_14cyc_XA_Y_XA_Y_123ln],
	(instrs FDIVv2f64, FDIVv4f32)>;
def KryoWrite_5cyc_X_noRSV_55ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_noRSV_55ln],
	(instregex "FN?M(ADD|SUB)Srrr")>;
def KryoWrite_6cyc_X_noRSV_57ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_X_noRSV_57ln],
	(instregex "FN?M(ADD|SUB)Drrr")>;
def KryoWrite_5cyc_X_noRSV_51ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_noRSV_51ln],
	(instrs FMLAv2f32, FMLSv2f32, FMLAv1i32_indexed, FMLSv1i32_indexed)>;
def KryoWrite_5cyc_X_X_56ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_X_56ln],
	(instrs FMLAv4f32, FMLSv4f32)>;
def KryoWrite_6cyc_X_X_61ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_X_X_61ln],
	(instrs FMLAv2f64, FMLSv2f64)>;
def KryoWrite_5cyc_X_noRSV_128ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_noRSV_128ln],
	(instrs FMLAv2i32_indexed, FMLSv2i32_indexed)>;
def KryoWrite_5cyc_X_X_131ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_X_131ln],
	(instrs FMLAv4i32_indexed, FMLSv4i32_indexed)>;
def KryoWrite_6cyc_X_X_134ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_X_X_134ln],
	(instrs FMLAv2i64_indexed, FMLSv2i64_indexed)>;
def KryoWrite_6cyc_X_noRSV_60ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_X_noRSV_60ln],
	(instrs FMLAv1i64_indexed, FMLSv1i64_indexed, FMULv1i64_indexed, FMULXv1i64_indexed)>;
def KryoWrite_1cyc_XY_45ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_45ln],
	(instregex "FMOV(XDHigh|DXHigh|DX)r")>;
def KryoWrite_1cyc_XY_noRSV_47ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_47ln],
	(instregex "FMOV(Di|Dr|Si|Sr|SWr|WSr|XDr|v.*_ns)")>;
def KryoWrite_5cyc_X_noRSV_53ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_noRSV_53ln],
	(instrs FMULv1i32_indexed, FMULXv1i32_indexed)>;
def KryoWrite_5cyc_X_noRSV_127ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_noRSV_127ln],
	(instrs FMULv2f32, FMULXv2f32, FMULv2i32_indexed, FMULXv2i32_indexed)>;
def KryoWrite_5cyc_X_X_130ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_X_130ln],
	(instrs FMULv4f32, FMULXv4f32, FMULv4i32_indexed, FMULXv4i32_indexed)>;
def KryoWrite_6cyc_X_X_133ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_X_X_133ln],
	(instrs FMULv2f64, FMULXv2f64, FMULv2i64_indexed, FMULXv2i64_indexed)>;
def KryoWrite_5cyc_X_noRSV_54ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_noRSV_54ln],
	(instrs FMULSrr, FNMULSrr, FMULX32)>;
def KryoWrite_6cyc_X_noRSV_59ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_X_noRSV_59ln],
	(instrs FMULDrr, FNMULDrr, FMULX64)>;
def KryoWrite_3cyc_XY_noRSV_28ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_28ln],
	(instrs FRECPEv1i32, FRECPEv1i64, FRSQRTEv1i32, FRSQRTEv1i64 )>;
def KryoWrite_3cyc_XY_noRSV_99ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_99ln],
	(instrs FRECPEv2f32, FRSQRTEv2f32)>;
def KryoWrite_3cyc_XY_XY_102ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_102ln],
	(instrs FRECPEv2f64, FRECPEv4f32, FRSQRTEv2f64, FRSQRTEv4f32)>;
def KryoWrite_5cyc_X_noRSV_52ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_noRSV_52ln],
	(instrs FRECPS32, FRSQRTS32)>;
def KryoWrite_6cyc_X_noRSV_58ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_X_noRSV_58ln],
	(instrs FRECPS64, FRSQRTS64)>;
def KryoWrite_5cyc_X_noRSV_126ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_noRSV_126ln],
	(instrs FRECPSv2f32, FRSQRTSv2f32)>;
def KryoWrite_5cyc_X_X_129ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_X_129ln],
	(instrs FRECPSv4f32, FRSQRTSv4f32)>;
def KryoWrite_6cyc_X_X_132ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_6cyc_X_X_132ln],
	(instrs FRECPSv2f64, FRSQRTSv2f64)>;
def KryoWrite_3cyc_XY_noRSV_50ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_50ln],
	(instrs FRECPXv1i32, FRECPXv1i64)>;
def KryoWrite_2cyc_XY_noRSV_39ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_39ln],
	(instregex "FRINT(A|I|M|N|P|X|Z)(S|D)r")>;
def KryoWrite_2cyc_XY_noRSV_108ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_108ln],
	(instregex "FRINT(A|I|M|N|P|X|Z)v2f32")>;
def KryoWrite_2cyc_XY_XY_109ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_109ln],
	(instregex "FRINT(A|I|M|N|P|X|Z)(v2f64|v4f32)")>;
def KryoWrite_12cyc_XA_Y_noRSV_42ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY]> {
	let Latency = 12; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_12cyc_XA_Y_noRSV_42ln],
	(instrs FSQRTSr)>;
def KryoWrite_21cyc_XA_Y_noRSV_42ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY]> {
	let Latency = 21; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_21cyc_XA_Y_noRSV_42ln],
	(instrs FSQRTDr)>;
def KryoWrite_12cyc_XA_Y_noRSV_120ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY]> {
	let Latency = 12; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_12cyc_XA_Y_noRSV_120ln],
	(instrs FSQRTv2f32)>;
def KryoWrite_21cyc_XA_Y_XA_Y_122ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY, KryoUnitXA, KryoUnitY]> {
	let Latency = 21; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_21cyc_XA_Y_XA_Y_122ln],
	(instrs FSQRTv4f32)>;
def KryoWrite_36cyc_XA_Y_XA_Y_122ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitY, KryoUnitXA, KryoUnitY]> {
	let Latency = 36; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_36cyc_XA_Y_XA_Y_122ln],
	(instrs FSQRTv2f64)>;
def KryoWrite_1cyc_X_201ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_X_201ln],
	(instregex "INSv.*")>;
def KryoWrite_3cyc_LS_255ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_255ln],
	(instregex "LD1(One(v16b|v8h|v4s|v2d)|i64)$")>;
def KryoWrite_4cyc_LS_X_270ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_LS_X_270ln],
	(instregex "LD1(i8|i16|i32)$")>;
def KryoWrite_3cyc_LS_noRSV_285ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_noRSV_285ln],
	(instregex "LD1One(v8b|v4h|v2s|v1d)$")>;
def KryoWrite_3cyc_LS_XY_289ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_289ln, WriteAdr],
	(instregex "LD1(One(v16b|v8h|v4s|v2d)|i64)_POST$")>;
def KryoWrite_4cyc_LS_XY_X_298ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_4cyc_LS_XY_X_298ln, WriteAdr],
	(instregex "LD1(i8|i16|i32)_POST$")>;
def KryoWrite_3cyc_LS_LS_LS_308ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_LS_308ln],
	(instregex "LD1Three(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_3cyc_LS_XY_noRSV_317ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_noRSV_317ln, WriteAdr],
	(instregex "LD1One(v8b|v4h|v2s|v1d)_POST$")>;
def KryoWrite_3cyc_LS_LS_LS_LS_328ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_LS_LS_328ln, WriteAdr],
	(instregex "LD1Four(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_3cyc_LS_XY_LS_LS_332ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_LS_332ln, WriteAdr],
	(instregex "LD1Three(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_3cyc_LS_LS_noRSV_noRSV_noRSV_348ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_noRSV_noRSV_noRSV_348ln],
	(instregex "LD1Three(v8b|v4h|v2s|v1d)$")>;
def KryoWrite_3cyc_LS_XY_LS_LS_LS_351ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_LS_LS_351ln],
	(instregex "LD1Four(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_3cyc_LS_LS_noRSV_noRSV_noRSV_noRSV_358ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_noRSV_noRSV_noRSV_noRSV_358ln],
	(instregex "LD1Four(v8b|v4h|v2s|v1d)$")>;
def KryoWrite_3cyc_LS_XY_LS_noRSV_noRSV_noRSV_360ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_noRSV_noRSV_noRSV_360ln, WriteAdr],
	(instregex "LD1Three(v8b|v4h|v2s|v1d)_POST$")>;
def KryoWrite_3cyc_LS_XY_LS_noRSV_noRSV_noRSV_noRSV_368ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 7;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_noRSV_noRSV_noRSV_noRSV_368ln, WriteAdr],
	(instregex "LD1Four(v8b|v4h|v2s|v1d)_POST$")>;
def KryoWrite_3cyc_LS_LS_281ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_281ln],
	(instregex "LD(1|2)Two(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_3cyc_LS_noRSV_noRSV_311ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_noRSV_noRSV_311ln],
	(instregex "LD(1|2)Two(v8b|v4h|v2s|v1d)$")>;
def KryoWrite_3cyc_LS_XY_LS_313ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_313ln, WriteAdr],
	(instregex "LD(1|2)Two(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_3cyc_LS_XY_noRSV_noRSV_334ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_noRSV_noRSV_334ln, WriteAdr],
	(instregex "LD(1|2)Two(v8b|v4h|v2s|v1d)_POST$")>;
def KryoWrite_3cyc_LS_256ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_256ln],
	(instregex "LD1R(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_3cyc_LS_noRSV_286ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_noRSV_286ln],
	(instregex "LD1R(v8b|v4h|v2s|v1d)$")>;
def KryoWrite_3cyc_LS_XY_290ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_290ln, WriteAdr],
	(instregex "LD1R(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_3cyc_LS_XY_noRSV_318ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_noRSV_318ln, WriteAdr],
	(instregex "LD1R(v8b|v4h|v2s|v1d)_POST$")>;
def KryoWrite_3cyc_LS_257ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_257ln],
	(instregex "LD2i64$")>;
def KryoWrite_3cyc_LS_XY_291ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_291ln, WriteAdr],
	(instregex "LD2i64_POST$")>;
def KryoWrite_4cyc_LS_X_X_296ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_4cyc_LS_X_X_296ln],
	(instregex "LD2(i8|i16|i32)$")>;
def KryoWrite_4cyc_LS_XY_X_X_321ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_4cyc_LS_XY_X_X_321ln, WriteAdr],
	(instregex "LD2(i8|i16|i32)_POST$")>;
def KryoWrite_3cyc_LS_LS_282ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_282ln],
	(instregex "LD2R(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_3cyc_LS_noRSV_noRSV_312ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_noRSV_noRSV_312ln],
	(instregex "LD2R(v8b|v4h|v2s|v1d)$")>;
def KryoWrite_3cyc_LS_XY_LS_314ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_314ln, WriteAdr],
	(instregex "LD2R(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_3cyc_LS_XY_noRSV_noRSV_335ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_noRSV_noRSV_335ln, WriteAdr],
	(instregex "LD2R(v8b|v4h|v2s|v1d)_POST$")>;
def KryoWrite_3cyc_LS_LS_283ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_283ln],
	(instregex "LD3i64$")>;
def KryoWrite_3cyc_LS_LS_LS_309ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_LS_309ln],
	(instregex "LD3Threev2d$")>;
def KryoWrite_3cyc_LS_XY_LS_315ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_315ln, WriteAdr],
	(instregex "LD3i64_POST$")>;
def KryoWrite_4cyc_LS_X_X_X_320ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_4cyc_LS_X_X_X_320ln],
	(instregex "LD3(i8|i16|i32)$")>;
def KryoWrite_3cyc_LS_XY_LS_LS_331ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_LS_331ln, WriteAdr],
	(instregex "LD3Threev2d_POST$")>;
def KryoWrite_4cyc_LS_XY_X_X_X_338ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_4cyc_LS_XY_X_X_X_338ln, WriteAdr],
	(instregex "LD3(i8|i16|i32)_POST$")>;
def KryoWrite_4cyc_LS_LS_X_X_X_noRSV_noRSV_noRSV_373ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 8;
}
def : InstRW<[KryoWrite_4cyc_LS_LS_X_X_X_noRSV_noRSV_noRSV_373ln],
	(instregex "LD3Three(v8b|v4h|v2s)$")>;
def KryoWrite_4cyc_LS_XY_LS_X_X_X_noRSV_noRSV_noRSV_380ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitX, KryoUnitX,
                   KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 9;
}
def : InstRW<[KryoWrite_4cyc_LS_XY_LS_X_X_X_noRSV_noRSV_noRSV_380ln, WriteAdr],
	(instregex "LD3Three(v8b|v4h|v2s)_POST$")>;
def KryoWrite_4cyc_LS_LS_X_X_X_LS_LS_X_X_X_381ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitLS, KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 10;
}
def : InstRW<[KryoWrite_4cyc_LS_LS_X_X_X_LS_LS_X_X_X_381ln],
	(instregex "LD3Three(v16b|v8h|v4s)$")>;
def KryoWrite_4cyc_LS_LS_X_X_X_LS_XY_LS_X_X_X_383ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitX, KryoUnitX,
                   KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 11;
}
def : InstRW<[KryoWrite_4cyc_LS_LS_X_X_X_LS_XY_LS_X_X_X_383ln, WriteAdr],
	(instregex "LD3Three(v16b|v8h|v4s)_POST$")>;
def KryoWrite_3cyc_LS_LS_LS_310ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_LS_310ln],
	(instregex "LD3R(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_3cyc_LS_XY_LS_LS_333ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_LS_333ln, WriteAdr],
	(instregex "LD3R(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_3cyc_LS_LS_noRSV_noRSV_noRSV_349ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_noRSV_noRSV_noRSV_349ln],
	(instregex "LD3R(v8b|v4h|v2s|v1d)$")>;
def KryoWrite_3cyc_LS_XY_LS_noRSV_noRSV_noRSV_361ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_noRSV_noRSV_noRSV_361ln, WriteAdr],
	(instregex "LD3R(v8b|v4h|v2s|v1d)_POST$")>;
def KryoWrite_3cyc_LS_LS_284ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_284ln],
	(instregex "LD4i64$")>;
def KryoWrite_3cyc_LS_XY_LS_316ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_316ln, WriteAdr],
	(instregex "LD4i64_POST$")>;
def KryoWrite_3cyc_LS_LS_LS_LS_329ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_LS_LS_329ln],
	(instregex "LD4Four(v2d)$")>;
def KryoWrite_4cyc_LS_X_X_X_X_337ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_4cyc_LS_X_X_X_X_337ln],
	(instregex "LD4(i8|i16|i32)$")>;
def KryoWrite_3cyc_LS_XY_LS_LS_LS_350ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_LS_LS_350ln, WriteAdr],
	(instregex "LD4Four(v2d)_POST$")>;
def KryoWrite_4cyc_LS_XY_X_X_X_X_355ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_4cyc_LS_XY_X_X_X_X_355ln, WriteAdr],
	(instregex "LD4(i8|i16|i32)_POST$")>;
def KryoWrite_4cyc_LS_LS_X_X_X_X_noRSV_noRSV_noRSV_noRSV_382ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 10;
}
def : InstRW<[KryoWrite_4cyc_LS_LS_X_X_X_X_noRSV_noRSV_noRSV_noRSV_382ln],
	(instregex "LD4Four(v8b|v4h|v2s)$")>;
def KryoWrite_4cyc_LS_XY_LS_X_X_X_X_noRSV_noRSV_noRSV_noRSV_384ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 11;
}
def : InstRW<[KryoWrite_4cyc_LS_XY_LS_X_X_X_X_noRSV_noRSV_noRSV_noRSV_384ln, WriteAdr],
	(instregex "LD4Four(v8b|v4h|v2s)_POST$")>;
def KryoWrite_4cyc_LS_LS_X_X_X_X_LS_LS_X_X_X_X_386ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitLS, KryoUnitLS, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 12;
}
def : InstRW<[KryoWrite_4cyc_LS_LS_X_X_X_X_LS_LS_X_X_X_X_386ln],
	(instregex "LD4Four(v16b|v8h|v4s)$")>;
def KryoWrite_4cyc_LS_LS_X_X_X_X_LS_XY_LS_X_X_X_X_389ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitX,
                   KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 13;
}
def : InstRW<[KryoWrite_4cyc_LS_LS_X_X_X_X_LS_XY_LS_X_X_X_X_389ln, WriteAdr],
	(instregex "LD4Four(v16b|v8h|v4s)_POST$")>;
def KryoWrite_3cyc_LS_LS_LS_LS_330ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_LS_LS_330ln],
	(instregex "LD4R(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_3cyc_LS_XY_LS_LS_LS_352ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS, KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_LS_LS_352ln, WriteAdr],
	(instregex "LD4R(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_3cyc_LS_LS_noRSV_noRSV_noRSV_noRSV_359ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_noRSV_noRSV_noRSV_noRSV_359ln],
	(instregex "LD4R(v8b|v4h|v2s|v1d)$")>;
def KryoWrite_3cyc_LS_XY_LS_noRSV_noRSV_noRSV_noRSV_369ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 7;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_noRSV_noRSV_noRSV_noRSV_369ln, WriteAdr],
	(instregex "LD4R(v8b|v4h|v2s|v1d)_POST$")>;
def KryoWrite_3cyc_LS_LS_400ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_400ln],
	(instregex "LDAX?R(B|H|W|X)")>;
def : InstRW<[KryoWrite_3cyc_LS_LS_400ln, WriteLDHi],
	(instregex "LDAXP(W|X)")>;
def KryoWrite_3cyc_LS_LS_401ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_401ln, WriteLDHi],
	(instrs LDNPQi)>;
def KryoWrite_3cyc_LS_noRSV_noRSV_408ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_noRSV_noRSV_408ln, WriteLDHi],
	(instrs LDNPDi, LDNPSi)>;
def KryoWrite_3cyc_LS_394ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_394ln, WriteLDHi],
	(instrs LDNPWi, LDNPXi)>;
def KryoWrite_3cyc_LS_LS_402ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_402ln, WriteLDHi],
	(instrs LDPQi)>;
def KryoWrite_3cyc_LS_noRSV_noRSV_409ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_noRSV_noRSV_409ln, WriteLDHi],
	(instrs LDPDi, LDPSi)>;
def KryoWrite_3cyc_LS_XY_LS_410ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY, KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_LS_410ln, WriteLDHi, WriteAdr],
	(instregex "LDPQ(post|pre)")>;
def KryoWrite_3cyc_LS_XY_noRSV_noRSV_411ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_noRSV_noRSV_411ln, WriteLDHi, WriteAdr],
	(instregex "LDP(D|S)(post|pre)")>;
def KryoWrite_3cyc_LS_393ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_393ln, WriteLDHi],
	(instrs LDPWi, LDPXi)>;
def KryoWrite_3cyc_LS_XY_403ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_403ln, WriteLDHi, WriteAdr],
	(instregex "LDP(W|X)(post|pre)")>;
def KryoWrite_4cyc_LS_395ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_LS_395ln, WriteLDHi],
	(instrs LDPSWi)>;
def KryoWrite_4cyc_LS_XY_405ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_LS_XY_405ln, WriteLDHi, WriteAdr],
	(instrs LDPSWpost, LDPSWpre)>;
def KryoWrite_3cyc_LS_264ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_264ln],
	(instrs LDRQui, LDRQl)>;
def KryoWrite_4cyc_X_LS_271ln :
	SchedWriteRes<[KryoUnitX, KryoUnitLS]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_LS_271ln],
	(instrs LDRQroW, LDRQroX)>;
def KryoWrite_3cyc_LS_noRSV_287ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_noRSV_287ln],
	(instregex "LDR((D|S)l|(D|S|H|B)ui)")>;
def KryoWrite_3cyc_LS_XY_293ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_293ln, WriteAdr],
	(instrs LDRQpost, LDRQpre)>;
def KryoWrite_4cyc_X_LS_noRSV_297ln :
	SchedWriteRes<[KryoUnitX, KryoUnitLS]> {
	let Latency = 4; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_4cyc_X_LS_noRSV_297ln],
	(instregex "LDR(D|S|H|B)ro(W|X)")>;
def KryoWrite_3cyc_LS_XY_noRSV_319ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_noRSV_319ln, WriteAdr],
	(instregex "LDR(D|S|H|B)(post|pre)")>;
def KryoWrite_3cyc_LS_261ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_261ln],
	(instregex "LDR(BB|HH|W|X)ui")>;
def KryoWrite_3cyc_LS_XY_292ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_XY_292ln, WriteAdr],
	(instregex "LDR(BB|HH|W|X)(post|pre)")>;
def KryoWrite_4cyc_X_LS_272ln :
	SchedWriteRes<[KryoUnitX, KryoUnitLS]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_LS_272ln],
	(instregex "(LDR(BB|HH|W|X)ro(W|X)|PRFMro(W|X))")>;
def KryoWrite_3cyc_LS_262ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_262ln],
	(instrs LDRWl, LDRXl)>;
def KryoWrite_4cyc_LS_268ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_LS_268ln],
	(instregex "LDRS(BW|BX|HW|HX|W)ui")>;
def KryoWrite_5cyc_X_LS_273ln :
	SchedWriteRes<[KryoUnitX, KryoUnitLS]> {
	let Latency = 5; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_5cyc_X_LS_273ln],
	(instregex "LDRS(BW|BX|HW|HX|W)ro(W|X)")>;
def KryoWrite_4cyc_LS_XY_294ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitXY]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_LS_XY_294ln, WriteAdr],
	(instregex "LDRS(BW|BX|HW|HX|W)(post|pre)")>;
def KryoWrite_4cyc_LS_269ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_LS_269ln],
	(instrs LDRSWl)>;
def KryoWrite_3cyc_LS_260ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_260ln],
	(instregex "LDTR(B|H|W|X)i")>;
def KryoWrite_4cyc_LS_267ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_LS_267ln],
	(instregex "LDTRS(BW|BX|HW|HX|W)i")>;
def KryoWrite_3cyc_LS_263ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_263ln],
	(instrs LDURQi)>;
def KryoWrite_3cyc_LS_noRSV_288ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_noRSV_288ln],
	(instregex "LDUR(D|S|H|B)i")>;
def KryoWrite_3cyc_LS_259ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_259ln],
	(instregex "LDUR(BB|HH|W|X)i")>;
def KryoWrite_4cyc_LS_266ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_LS_266ln],
	(instregex "LDURS(B|H)?(W|X)i")>;
def KryoWrite_3cyc_LS_258ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_258ln, WriteLDHi],
	(instregex "LDXP(W|X)")>;
def KryoWrite_3cyc_LS_258_1ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 3; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_3cyc_LS_258_1ln],
	(instregex "LDXR(B|H|W|X)")>;
def KryoWrite_2cyc_XY_XY_137ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_137ln],
	(instrs LSLVWr, LSLVXr)>;
def KryoWrite_1cyc_XY_135ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_135ln],
	(instregex "(LS|AS|RO)RV(W|X)r")>;
def KryoWrite_4cyc_X_84ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_4cyc_X_84ln],
	(instrs MADDWrrr, MSUBWrrr)>;
def KryoWrite_5cyc_X_85ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_5cyc_X_85ln],
	(instrs MADDXrrr, MSUBXrrr)>;
def KryoWrite_4cyc_X_noRSV_188ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_188ln],
	(instregex "(MLA|MLS|MUL)(v8i8|v4i16|v2i32)(_indexed)?")>;
def KryoWrite_4cyc_X_X_192ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_X_192ln],
	(instregex "(MLA|MLS|MUL|SQR?DMULH)(v16i8|v8i16|v4i32)(_indexed)?")>;
def KryoWrite_1cyc_XY_noRSV_198ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_198ln],
	(instregex "(MOVI|MVNI)(D|v8b_ns|v2i32|v4i16|v2s_msl)")>;
def KryoWrite_1cyc_XY_XY_199ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_199ln],
	(instregex "(MOVI|MVNI)(v2d_ns|v16b_ns|v4i32|v8i16|v4s_msl)")>;
def KryoWrite_1cyc_X_89ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_X_89ln],
	(instrs MOVKWi, MOVKXi)>;
def KryoWrite_1cyc_XY_91ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_91ln],
	(instrs MOVNWi, MOVNXi)>;
def KryoWrite_1cyc_XY_90ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_90ln],
	(instrs MOVZWi, MOVZXi)>;
def KryoWrite_2cyc_XY_93ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_2cyc_XY_93ln],
	(instrs MRS)>;
def KryoWrite_0cyc_X_87ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_X_87ln],
	(instrs MSRpstateImm4)>;
def : InstRW<[KryoWrite_0cyc_X_87ln],
	(instrs MSRpstateImm1)>;
def KryoWrite_0cyc_XY_88ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_XY_88ln],
	(instrs MSR)>;
def KryoWrite_1cyc_XY_noRSV_143ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_143ln],
	(instregex "NEG(v8i8|v4i16|v2i32|v1i64)")>;
def KryoWrite_1cyc_XY_XY_145ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_145ln],
	(instregex "NEG(v16i8|v8i16|v4i32|v2i64)")>;
def KryoWrite_1cyc_XY_noRSV_193ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_193ln],
	(instrs NOTv8i8)>;
def KryoWrite_1cyc_XY_XY_194ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_194ln],
	(instrs NOTv16i8)>;
def KryoWrite_2cyc_XY_noRSV_234ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_234ln],
	(instrs PMULv8i8)>;
def KryoWrite_2cyc_XY_XY_236ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_236ln],
	(instrs PMULv16i8)>;
def KryoWrite_2cyc_XY_XY_235ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_235ln],
	(instrs PMULLv8i8, PMULLv16i8)>;
def KryoWrite_3cyc_XY_XY_237ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_237ln],
	(instrs PMULLv1i64, PMULLv2i64)>;
def KryoWrite_0cyc_LS_254ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_LS_254ln],
	(instrs PRFMl, PRFMui)>;
def KryoWrite_0cyc_LS_253ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_LS_253ln],
	(instrs PRFUMi)>;
def KryoWrite_6cyc_XY_X_noRSV_175ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitX]> {
	let Latency = 6; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_6cyc_XY_X_noRSV_175ln],
	(instregex "R(ADD|SUB)HNv.*")>;
def KryoWrite_2cyc_XY_204ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_2cyc_XY_204ln],
	(instrs RBITWr, RBITXr)>;
def KryoWrite_2cyc_XY_noRSV_218ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_noRSV_218ln],
	(instrs RBITv8i8)>;
def KryoWrite_2cyc_XY_XY_219ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_219ln],
	(instrs RBITv16i8)>;
def KryoWrite_1cyc_X_202ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_X_202ln],
	(instregex "REV(16|32)?(W|X)r")>;
def KryoWrite_1cyc_XY_noRSV_214ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_214ln],
	(instregex "REV(16|32|64)(v8i8|v4i16|v2i32)")>;
def KryoWrite_1cyc_XY_XY_216ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_216ln],
	(instregex "REV(16|32|64)(v16i8|v8i16|v4i32)")>;
def KryoWrite_3cyc_X_noRSV_244ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_X_noRSV_244ln],
	(instregex "S(L|R)I(d|(v8i8|v4i16|v2i32)_shift)")>;
def KryoWrite_3cyc_X_X_245ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_X_X_245ln],
	(instregex "S(L|R)I(v16i8|v8i16|v4i32|v2i64)_shift")>;
def KryoWrite_1cyc_XY_2ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_2ln, ReadI, ReadI],
	(instregex "SBCS?(W|X)r")>;
def KryoWrite_2cyc_XA_XA_XA_24ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitXA, KryoUnitXA]> {
	let Latency = 2; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_2cyc_XA_XA_XA_24ln],
	(instrs SHA1Crrr, SHA1Mrrr, SHA1Prrr)>;
def KryoWrite_1cyc_XY_noRSV_21ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_21ln],
	(instrs SHA1Hrr)>;
def KryoWrite_2cyc_X_X_23ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_X_X_23ln],
	(instrs SHA1SU0rrr, SHA1SU1rr, SHA256SU0rr)>;
def KryoWrite_4cyc_XA_XA_XA_25ln :
	SchedWriteRes<[KryoUnitXA, KryoUnitXA, KryoUnitXA]> {
	let Latency = 4; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_4cyc_XA_XA_XA_25ln],
	(instrs SHA256Hrrr, SHA256H2rrr)>;
def KryoWrite_3cyc_XY_XY_X_X_26ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY, KryoUnitX, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_X_X_26ln],
	(instrs SHA256SU1rrr)>;
def KryoWrite_4cyc_X_noRSV_189ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_189ln],
	(instregex "SQR?DMULH(v8i8|v4i16|v1i32|v2i32|v1i16)(_indexed)?")>;
def KryoWrite_3cyc_XY_noRSV_68ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_68ln],
	(instregex "SQ(ABS|NEG)(v1i8|v1i16|v1i32|v1i64)")>;
def KryoWrite_3cyc_XY_noRSV_157ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_157ln],
	(instregex "SQ(ABS|NEG)(v8i8|v4i16|v2i32)")>;
def KryoWrite_3cyc_XY_XY_164ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_164ln],
	(instregex "SQ(ABS|NEG)(v16i8|v8i16|v4i32|v2i64)")>;
def KryoWrite_4cyc_X_noRSV_190ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_4cyc_X_noRSV_190ln],
	(instregex "SQD(MLAL|MLSL|MULL)(i16|i32)")>;
def KryoWrite_0cyc_LS_Y_274ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_274ln],
	(instregex "ST1(One(v8b|v4h|v2s|v1d|v16b|v8h|v4s|v2d)|(i8|i16|i32|i64)|Two(v8b|v4h|v2s|v1d))$")>;
def KryoWrite_1cyc_LS_Y_X_301ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_X_301ln],
	(instregex "ST1(One(v8b|v4h|v2s|v1d|v16b|v8h|v4s|v2d)|(i8|i16|i32|i64)|Two(v8b|v4h|v2s|v1d))_POST$")>;
def KryoWrite_1cyc_LS_Y_XY_305ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_XY_305ln],
	(instregex "ST1(One(v16b|v8h|v4s|v2d)|Two(v8b|v4h|v2s|v1d))_POST$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_323ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 4;
}
def : InstRW<[WriteAdr, KryoWrite_0cyc_LS_Y_LS_Y_323ln],
	(instregex "ST1(Two(v16b|v8h|v4s|v2d)|(Three|Four)(v8b|v4h|v2s|v1d))_POST$")>;
def KryoWrite_1cyc_LS_Y_XY_LS_Y_345ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitXY, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_1cyc_LS_Y_XY_LS_Y_345ln],
	(instregex "ST1(Two(v16b|v8h|v4s|v2d)|(Three|Four)(v8b|v4h|v2s|v1d))$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_LS_Y_356ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY, KryoUnitLS,
                   KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_LS_Y_356ln],
	(instregex "ST1Three(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_1cyc_LS_Y_XY_LS_Y_LS_Y_366ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitXY, KryoUnitLS, KryoUnitY,
                   KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 7;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_XY_LS_Y_LS_Y_366ln],
	(instregex "ST1Three(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_LS_Y_LS_Y_371ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY, KryoUnitLS,
                   KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 8;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_LS_Y_LS_Y_371ln],
	(instregex "ST1Four(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_XY_LS_Y_LS_Y_377ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY, KryoUnitXY,
                   KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 9;
}
def : InstRW<[WriteAdr, KryoWrite_0cyc_LS_Y_LS_Y_XY_LS_Y_LS_Y_377ln],
	(instregex "ST1Four(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_0cyc_LS_Y_275ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_275ln],
	(instregex "ST2(Two(v8b|v4h|v2s|v1d|v16b|v8h|v4s|v2d)|(i8|i16|i32|i64))$")>;
def KryoWrite_1cyc_LS_Y_XY_306ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_XY_306ln],
	(instregex "ST2(Two(v8b|v4h|v2s|v1d)|(i8|i16|i32|i64))_POST$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_322ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_322ln],
	(instregex "ST2Two(v16b|v8h|v4s|v2d)$")>;
def KryoWrite_1cyc_LS_Y_XY_LS_Y_344ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitXY, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 5;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_XY_LS_Y_344ln],
	(instregex "ST2Two(v16b|v8h|v4s|v2d)_POST$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_324ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_324ln],
	(instregex "ST3(Threev1d|(i8|i16|i32|i64))$")>;
def KryoWrite_1cyc_LS_Y_XY_LS_Y_346ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitXY, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 5;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_XY_LS_Y_346ln],
	(instregex "ST3(Threev1d|(i8|i16|i32|i64))_POST$")>;
def KryoWrite_1cyc_X_X_LS_Y_LS_Y_353ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitLS,
                   KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_1cyc_X_X_LS_Y_LS_Y_353ln],
	(instregex "ST3Three(v8b|v4h|v2s)$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_LS_Y_357ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY, KryoUnitLS,
                   KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_LS_Y_357ln],
	(instregex "ST3Threev2d$")>;
def KryoWrite_1cyc_X_X_LS_Y_XY_LS_Y_363ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitXY,
                   KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 7;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_X_X_LS_Y_XY_LS_Y_363ln],
	(instregex "ST3Three(v8b|v4h|v2s)_POST$")>;
def KryoWrite_1cyc_LS_Y_XY_LS_Y_LS_Y_367ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitXY, KryoUnitLS, KryoUnitY,
                   KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 7;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_XY_LS_Y_LS_Y_367ln],
	(instregex "ST3Threev2d_POST$")>;
def KryoWrite_1cyc_X_X_LS_Y_LS_Y_X_X_LS_Y_LS_Y_385ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitLS,
                   KryoUnitY, KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY,
                   KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 12;
}
def : InstRW<[KryoWrite_1cyc_X_X_LS_Y_LS_Y_X_X_LS_Y_LS_Y_385ln],
	(instregex "ST3Three(v16b|v8h|v4s)$")>;
def KryoWrite_1cyc_X_X_LS_Y_LS_Y_X_X_LS_Y_XY_LS_Y_388ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitLS,
                   KryoUnitY, KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY,
                   KryoUnitXY, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 13;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_X_X_LS_Y_LS_Y_X_X_LS_Y_XY_LS_Y_388ln],
	(instregex "ST3Three(v16b|v8h|v4s)_POST$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_325ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_325ln],
	(instregex "ST4(Fourv1d|(i8|i16|i32|i64))$")>;
def KryoWrite_1cyc_LS_Y_XY_LS_Y_347ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitXY, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 5;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_XY_LS_Y_347ln],
	(instregex "ST4(Fourv1d|(i8|i16|i32|i64))_POST$")>;
def KryoWrite_1cyc_X_X_LS_Y_X_X_LS_Y_370ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitX,
                   KryoUnitX, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 8;
}
def : InstRW<[KryoWrite_1cyc_X_X_LS_Y_X_X_LS_Y_370ln],
	(instregex "ST4Four(v8b|v4h|v2s)$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_LS_Y_LS_Y_372ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY, KryoUnitLS,
                   KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 8;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_LS_Y_LS_Y_372ln],
	(instregex "ST4Fourv2d$")>;
def KryoWrite_1cyc_X_X_LS_Y_XY_X_X_LS_Y_375ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitXY,
                   KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 9;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_X_X_LS_Y_XY_X_X_LS_Y_375ln],
	(instregex "ST4Four(v8b|v4h|v2s)_POST$")>;
def KryoWrite_0cyc_LS_Y_LS_Y_XY_LS_Y_LS_Y_379ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY, KryoUnitXY,
                   KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 9;
}
def : InstRW<[WriteAdr, KryoWrite_0cyc_LS_Y_LS_Y_XY_LS_Y_LS_Y_379ln],
	(instregex "ST4Fourv2d_POST$")>;
def KryoWrite_1cyc_X_X_LS_Y_X_X_LS_Y_X_X_LS_Y_X_X_LS_Y_390ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitX,
                   KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitX, KryoUnitX,
                   KryoUnitLS, KryoUnitY, KryoUnitX, KryoUnitX, KryoUnitLS,
                   KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 16;
}
def : InstRW<[KryoWrite_1cyc_X_X_LS_Y_X_X_LS_Y_X_X_LS_Y_X_X_LS_Y_390ln],
	(instregex "ST4Four(v16b|v8h|v4s)$")>;
def KryoWrite_1cyc_X_X_LS_Y_X_X_LS_Y_X_X_LS_Y_XY_X_X_LS_Y_392ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitX,
                   KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitX, KryoUnitX,
                   KryoUnitLS, KryoUnitY, KryoUnitXY, KryoUnitX, KryoUnitX,
                   KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 17;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_X_X_LS_Y_X_X_LS_Y_X_X_LS_Y_XY_X_X_LS_Y_392ln],
	(instregex "ST4Four(v16b|v8h|v4s)_POST$")>;
def KryoWrite_0cyc_LS_LS_Y_299ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_0cyc_LS_LS_Y_299ln],
	(instregex "STLR(B|H|W|X)")>;
def KryoWrite_3cyc_LS_LS_Y_307ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitLS, KryoUnitY]> {
	let Latency = 3; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_3cyc_LS_LS_Y_307ln],
	(instregex "STLX(P(W|X)|R(B|H|W|X))")>;
def KryoWrite_0cyc_LS_Y_276ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_276ln],
	(instrs STNPDi, STNPSi)>;
def KryoWrite_0cyc_LS_Y_LS_Y_326ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_326ln],
	(instrs STNPQi)>;
def KryoWrite_0cyc_LS_Y_280ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_280ln],
	(instrs STNPWi, STNPXi)>;
def KryoWrite_0cyc_LS_Y_277ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_277ln],
	(instregex "STP(D|S)i")>;
def KryoWrite_1cyc_LS_Y_X_303ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_X_303ln],
	(instregex "STP(D|S)(post|pre)")>;
def KryoWrite_0cyc_LS_Y_LS_Y_327ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_LS_Y_327ln],
	(instrs STPQi)>;
def KryoWrite_1cyc_LS_Y_X_LS_Y_343ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitX, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 5;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_X_LS_Y_343ln],
	(instrs STPQpost, STPQpre)>;
def KryoWrite_0cyc_LS_Y_279ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_279ln],
	(instregex "STP(W|X)i")>;
def KryoWrite_1cyc_LS_X_Y_300ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitX, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_X_Y_300ln],
	(instregex "STP(W|X)(post|pre)")>;
def KryoWrite_0cyc_LS_Y_278ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_278ln],
	(instregex "STR(Q|D|S|H|B)ui")>;
def KryoWrite_1cyc_X_LS_Y_295ln :
	SchedWriteRes<[KryoUnitX, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_1cyc_X_LS_Y_295ln],
	(instregex "STR(D|S|H|B)ro(W|X)")>;
def KryoWrite_1cyc_LS_Y_X_304ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_Y_X_304ln],
	(instregex "STR(Q|D|S|H|B)(post|pre)")>;
def KryoWrite_2cyc_X_LS_Y_XY_LS_Y_354ln :
	SchedWriteRes<[KryoUnitX, KryoUnitLS, KryoUnitY, KryoUnitXY, KryoUnitLS,
                   KryoUnitY]> {
	let Latency = 2; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_2cyc_X_LS_Y_XY_LS_Y_354ln],
	(instregex "STRQro(W|X)")>;
def KryoWrite_0cyc_LS_Y_399ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_399ln],
	(instregex "STR(BB|HH|W|X)ui")>;
def KryoWrite_1cyc_X_LS_Y_406ln :
	SchedWriteRes<[KryoUnitX, KryoUnitLS, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_1cyc_X_LS_Y_406ln],
	(instregex "STR(BB|HH|W|X)ro(W|X)")>;
def KryoWrite_1cyc_LS_X_Y_407ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitX, KryoUnitY]> {
	let Latency = 1; let NumMicroOps = 3;
}
def : InstRW<[WriteAdr, KryoWrite_1cyc_LS_X_Y_407ln],
	(instregex "STR(BB|HH|W|X)(post|pre)")>;
def KryoWrite_0cyc_LS_Y_398ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_398ln],
	(instregex "STTR(B|H|W|X)i")>;
def KryoWrite_0cyc_LS_Y_396ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_396ln],
	(instregex "STUR(Q|D|S|H|B)i")>;
def KryoWrite_0cyc_LS_Y_397ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 0; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_0cyc_LS_Y_397ln],
	(instregex "STUR(BB|HH|W|X)i")>;
def KryoWrite_3cyc_LS_Y_404ln :
	SchedWriteRes<[KryoUnitLS, KryoUnitY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_LS_Y_404ln],
	(instregex "STX(P(W|X)|R(B|H|W|X))")>;
def KryoWrite_3cyc_XY_noRSV_160ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_160ln],
	(instregex "^(SU|US)QADD(v8i8|v4i16|v2i32)")>;
def KryoWrite_3cyc_XY_XY_167ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_167ln],
	(instregex "^(SU|US)QADD(v16i8|v8i16|v4i32|v2i64)")>;
def KryoWrite_1cyc_XY_1ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_1cyc_XY_1ln, ReadI],
	(instregex "SUBS?(W|X)ri")>;
def KryoWrite_2cyc_XY_XY_5ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_5ln, ReadI, ReadIEReg],
	(instregex "SUBS?(W|X)rx")>;
def KryoWrite_2cyc_XY_XY_5_1ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 2; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_2cyc_XY_XY_5_1ln, ReadI, ReadISReg],
	(instregex "SUBS?(W|X)rs")>;
def KryoWrite_1cyc_XY_noRSV_6ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_6ln, ReadI, ReadI],
	(instregex "SUBS?(W|X)rr")>;
def KryoWrite_0cyc_LS_9ln :
	SchedWriteRes<[KryoUnitLS]> {
	let Latency = 0; let NumMicroOps = 1;
}
def : InstRW<[KryoWrite_0cyc_LS_9ln],
	(instregex "SYSL?xt")>;
def KryoWrite_1cyc_X_noRSV_205ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_noRSV_205ln],
	(instrs TBLv8i8One)>;
def KryoWrite_1cyc_X_X_208ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_X_208ln],
	(instrs TBLv16i8One)>;
def KryoWrite_2cyc_X_X_X_noRSV_222ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 2; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_2cyc_X_X_X_noRSV_222ln],
	(instrs TBLv8i8Two)>;
def KryoWrite_2cyc_X_X_X_X_X_X_224ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX]> {
	let Latency = 2; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_2cyc_X_X_X_X_X_X_224ln],
	(instrs TBLv16i8Two)>;
def KryoWrite_3cyc_X_X_X_X_X_noRSV_225ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 6;
}
def : InstRW<[KryoWrite_3cyc_X_X_X_X_X_noRSV_225ln],
	(instrs TBLv8i8Three)>;
def KryoWrite_3cyc_X_X_X_X_X_X_X_noRSV_228ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 8;
}
def : InstRW<[KryoWrite_3cyc_X_X_X_X_X_X_X_noRSV_228ln],
	(instrs TBLv8i8Four)>;
def KryoWrite_4cyc_X_X_X_X_X_X_X_X_XY_X_X_230ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitXY, KryoUnitX,
                   KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 11;
}
def : InstRW<[KryoWrite_4cyc_X_X_X_X_X_X_X_X_XY_X_X_230ln],
	(instrs TBLv16i8Three)>;
def KryoWrite_4cyc_X_X_X_X_X_X_X_X_X_X_XY_X_X_X_X_232ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitXY, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 15;
}
def : InstRW<[KryoWrite_4cyc_X_X_X_X_X_X_X_X_X_X_XY_X_X_X_X_232ln],
	(instrs TBLv16i8Four)>;
def KryoWrite_2cyc_X_X_noRSV_220ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 2; let NumMicroOps = 3;
}
def : InstRW<[KryoWrite_2cyc_X_X_noRSV_220ln],
	(instrs TBXv8i8One)>;
def KryoWrite_2cyc_X_X_X_X_221ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 2; let NumMicroOps = 4;
}
def : InstRW<[KryoWrite_2cyc_X_X_X_X_221ln],
	(instrs TBXv16i8One)>;
def KryoWrite_3cyc_X_X_X_X_noRSV_223ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 5;
}
def : InstRW<[KryoWrite_3cyc_X_X_X_X_noRSV_223ln],
	(instrs TBXv8i8Two)>;
def KryoWrite_4cyc_X_X_X_X_X_X_noRSV_226ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 7;
}
def : InstRW<[KryoWrite_4cyc_X_X_X_X_X_X_noRSV_226ln],
	(instrs TBXv8i8Three)>;
def KryoWrite_3cyc_X_X_X_X_X_X_X_X_227ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 3; let NumMicroOps = 8;
}
def : InstRW<[KryoWrite_3cyc_X_X_X_X_X_X_X_X_227ln],
	(instrs TBXv16i8Two)>;
def KryoWrite_4cyc_X_X_X_X_X_X_X_X_noRSV_229ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 4; let NumMicroOps = 9;
}
def : InstRW<[KryoWrite_4cyc_X_X_X_X_X_X_X_X_noRSV_229ln],
	(instrs TBXv8i8Four)>;
def KryoWrite_5cyc_X_X_X_X_X_X_X_X_X_XY_X_X_X_231ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitXY,
                   KryoUnitX, KryoUnitX, KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 13;
}
def : InstRW<[KryoWrite_5cyc_X_X_X_X_X_X_X_X_X_XY_X_X_X_231ln],
	(instrs TBXv16i8Three)>;
def KryoWrite_5cyc_X_X_X_X_X_X_X_X_X_X_X_XY_X_X_X_X_X_233ln :
    SchedWriteRes<[KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitXY, KryoUnitX, KryoUnitX, KryoUnitX,
                   KryoUnitX, KryoUnitX]> {
	let Latency = 5; let NumMicroOps = 17;
}
def : InstRW<[KryoWrite_5cyc_X_X_X_X_X_X_X_X_X_X_X_XY_X_X_X_X_X_233ln],
	(instrs TBXv16i8Four)>;
def KryoWrite_1cyc_XY_XY_217ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_XY_217ln],
	(instregex "((TRN1|TRN2|ZIP1|UZP1|UZP2)v2i64|ZIP2(v2i64|v4i32|v8i16|v16i8))")>;
def KryoWrite_1cyc_X_X_211ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_X_211ln],
	(instregex "(TRN1|TRN2)(v4i32|v8i16|v16i8)")>;
def KryoWrite_1cyc_X_XY_213ln :
	SchedWriteRes<[KryoUnitX, KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_XY_213ln],
	(instregex "(TRN1|TRN2)(v2i32|v4i16|v8i8)")>;
def KryoWrite_3cyc_XY_noRSV_156ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_noRSV_156ln],
	(instrs URECPEv2i32, URSQRTEv2i32)>;
def KryoWrite_3cyc_XY_XY_168ln :
	SchedWriteRes<[KryoUnitXY, KryoUnitXY]> {
	let Latency = 3; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_3cyc_XY_XY_168ln],
	(instrs URECPEv4i32, URSQRTEv4i32)>;
def KryoWrite_1cyc_X_X_210ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_X_210ln],
	(instregex "(UZP1|UZP2)(v4i32|v8i16|v16i8)")>;
def KryoWrite_1cyc_X_noRSV_206ln :
	SchedWriteRes<[KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_noRSV_206ln],
	(instregex "(UZP1|UZP2|ZIP1|ZIP2)(v2i32|v4i16|v8i8)")>;
def KryoWrite_1cyc_XY_noRSV_215ln :
	SchedWriteRes<[KryoUnitXY]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_XY_noRSV_215ln],
	(instregex "XTNv.*")>;
def KryoWrite_1cyc_X_X_209ln :
	SchedWriteRes<[KryoUnitX, KryoUnitX]> {
	let Latency = 1; let NumMicroOps = 2;
}
def : InstRW<[KryoWrite_1cyc_X_X_209ln],
	(instregex "ZIP1(v4i32|v8i16|v16i8)")>;
