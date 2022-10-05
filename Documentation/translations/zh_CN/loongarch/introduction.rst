.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/loongarch/introduction.rst
:Translator: Huacai Chen <chenhuacai@loongson.cn>

=============
LoongArch介绍
=============

LoongArch是一种新的RISC ISA，在一定程度上类似于MIPS和RISC-V。LoongArch指令集
包括一个精简32位版（LA32R）、一个标准32位版（LA32S）、一个64位版（LA64）。
LoongArch定义了四个特权级（PLV0~PLV3），其中PLV0是最高特权级，用于内核；而PLV3
是最低特权级，用于应用程序。本文档介绍了LoongArch的寄存器、基础指令集、虚拟内
存以及其他一些主题。

寄存器
======

LoongArch的寄存器包括通用寄存器（GPRs）、浮点寄存器（FPRs）、向量寄存器（VRs）
和用于特权模式（PLV0）的控制状态寄存器（CSRs）。

通用寄存器
----------

LoongArch包括32个通用寄存器（ ``$r0`` ~ ``$r31`` ），LA32中每个寄存器为32位宽，
LA64中每个寄存器为64位宽。 ``$r0`` 的内容总是固定为0，而其他寄存器在体系结构层面
没有特殊功能。（ ``$r1`` 算是一个例外，在BL指令中固定用作链接返回寄存器。）

内核使用了一套LoongArch寄存器约定，定义在LoongArch ELF psABI规范中，详细描述参见
:ref:`参考文献 <loongarch-references-zh_CN>`:

================= =============== =================== ==========
寄存器名          别名            用途                跨调用保持
================= =============== =================== ==========
``$r0``           ``$zero``       常量0               不使用
``$r1``           ``$ra``         返回地址            否
``$r2``           ``$tp``         TLS/线程信息指针    不使用
``$r3``           ``$sp``         栈指针              是
``$r4``-``$r11``  ``$a0``-``$a7`` 参数寄存器          否
``$r4``-``$r5``   ``$v0``-``$v1`` 返回值              否
``$r12``-``$r20`` ``$t0``-``$t8`` 临时寄存器          否
``$r21``          ``$u0``         每CPU变量基地址     不使用
``$r22``          ``$fp``         帧指针              是
``$r23``-``$r31`` ``$s0``-``$s8`` 静态寄存器          是
================= =============== =================== ==========

.. note::
    注意： ``$r21`` 寄存器在ELF psABI中保留未使用，但是在Linux内核用于保
    存每CPU变量基地址。该寄存器没有ABI命名，不过在内核中称为 ``$u0`` 。在
    一些遗留代码中有时可能见到 ``$v0`` 和 ``$v1`` ，它们是 ``$a0`` 和
    ``$a1`` 的别名，属于已经废弃的用法。

浮点寄存器
----------

当系统中存在FPU时，LoongArch有32个浮点寄存器（ ``$f0`` ~ ``$f31`` ）。在LA64
的CPU核上，每个寄存器均为64位宽。

浮点寄存器的使用约定与LoongArch ELF psABI规范的描述相同：

================= ================== =================== ==========
寄存器名          别名               用途                跨调用保持
================= ================== =================== ==========
``$f0``-``$f7``   ``$fa0``-``$fa7``  参数寄存器          否
``$f0``-``$f1``   ``$fv0``-``$fv1``  返回值              否
``$f8``-``$f23``  ``$ft0``-``$ft15`` 临时寄存器          否
``$f24``-``$f31`` ``$fs0``-``$fs7``  静态寄存器          是
================= ================== =================== ==========

.. note::
    注意：在一些遗留代码中有时可能见到 ``$v0`` 和 ``$v1`` ，它们是
    ``$a0`` 和 ``$a1`` 的别名，属于已经废弃的用法。


向量寄存器
----------

LoongArch现有两种向量扩展：

- 128位向量扩展LSX（全称Loongson SIMD eXtention），
- 256位向量扩展LASX（全称Loongson Advanced SIMD eXtention）。

LSX使用 ``$v0`` ~ ``$v31`` 向量寄存器，而LASX则使用 ``$x0`` ~ ``$x31`` 。

浮点寄存器和向量寄存器是复用的，比如：在一个实现了LSX和LASX的核上， ``$x0`` 的
低128位与 ``$v0`` 共用， ``$v0`` 的低64位与 ``$f0`` 共用，其他寄存器依此类推。

控制状态寄存器
--------------

控制状态寄存器只能在特权模式（PLV0）下访问:

================= ==================================== ==========
地址              全称描述                             简称
================= ==================================== ==========
0x0               当前模式信息                         CRMD
0x1               异常前模式信息                       PRMD
0x2               扩展部件使能                         EUEN
0x3               杂项控制                             MISC
0x4               异常配置                             ECFG
0x5               异常状态                             ESTAT
0x6               异常返回地址                         ERA
0x7               出错(Faulting)虚拟地址               BADV
0x8               出错(Faulting)指令字                 BADI
0xC               异常入口地址                         EENTRY
0x10              TLB索引                              TLBIDX
0x11              TLB表项高位                          TLBEHI
0x12              TLB表项低位0                         TLBELO0
0x13              TLB表项低位1                         TLBELO1
0x18              地址空间标识符                       ASID
0x19              低半地址空间页全局目录基址           PGDL
0x1A              高半地址空间页全局目录基址           PGDH
0x1B              页全局目录基址                       PGD
0x1C              页表遍历控制低半部分                 PWCL
0x1D              页表遍历控制高半部分                 PWCH
0x1E              STLB页大小                           STLBPS
0x1F              缩减虚地址配置                       RVACFG
0x20              CPU编号                              CPUID
0x21              特权资源配置信息1                    PRCFG1
0x22              特权资源配置信息2                    PRCFG2
0x23              特权资源配置信息3                    PRCFG3
0x30+n (0≤n≤15)   数据保存寄存器                       SAVEn
0x40              定时器编号                           TID
0x41              定时器配置                           TCFG
0x42              定时器值                             TVAL
0x43              计时器补偿                           CNTC
0x44              定时器中断清除                       TICLR
0x60              LLBit相关控制                        LLBCTL
0x80              实现相关控制1                        IMPCTL1
0x81              实现相关控制2                        IMPCTL2
0x88              TLB重填异常入口地址                  TLBRENTRY
0x89              TLB重填异常出错(Faulting)虚地址      TLBRBADV
0x8A              TLB重填异常返回地址                  TLBRERA
0x8B              TLB重填异常数据保存                  TLBRSAVE
0x8C              TLB重填异常表项低位0                 TLBRELO0
0x8D              TLB重填异常表项低位1                 TLBRELO1
0x8E              TLB重填异常表项高位                  TLBEHI
0x8F              TLB重填异常前模式信息                TLBRPRMD
0x90              机器错误控制                         MERRCTL
0x91              机器错误信息1                        MERRINFO1
0x92              机器错误信息2                        MERRINFO2
0x93              机器错误异常入口地址                 MERRENTRY
0x94              机器错误异常返回地址                 MERRERA
0x95              机器错误异常数据保存                 MERRSAVE
0x98              高速缓存标签                         CTAG
0x180+n (0≤n≤3)   直接映射配置窗口n                    DMWn
0x200+2n (0≤n≤31) 性能监测配置n                        PMCFGn
0x201+2n (0≤n≤31) 性能监测计数器n                      PMCNTn
0x300             内存读写监视点整体控制               MWPC
0x301             内存读写监视点整体状态               MWPS
0x310+8n (0≤n≤7)  内存读写监视点n配置1                 MWPnCFG1
0x311+8n (0≤n≤7)  内存读写监视点n配置2                 MWPnCFG2
0x312+8n (0≤n≤7)  内存读写监视点n配置3                 MWPnCFG3
0x313+8n (0≤n≤7)  内存读写监视点n配置4                 MWPnCFG4
0x380             取指监视点整体控制                   FWPC
0x381             取指监视点整体状态                   FWPS
0x390+8n (0≤n≤7)  取指监视点n配置1                     FWPnCFG1
0x391+8n (0≤n≤7)  取指监视点n配置2                     FWPnCFG2
0x392+8n (0≤n≤7)  取指监视点n配置3                     FWPnCFG3
0x393+8n (0≤n≤7)  取指监视点n配置4                     FWPnCFG4
0x500             调试寄存器                           DBG
0x501             调试异常返回地址                     DERA
0x502             调试数据保存                         DSAVE
================= ==================================== ==========

ERA，TLBRERA，MERRERA和DERA有时也分别称为EPC，TLBREPC，MERREPC和DEPC。

基础指令集
==========

指令格式
--------

LoongArch的指令字长为32位，一共有9种基本指令格式（以及一些变体）:

=========== ==========================
格式名称    指令构成
=========== ==========================
2R          Opcode + Rj + Rd
3R          Opcode + Rk + Rj + Rd
4R          Opcode + Ra + Rk + Rj + Rd
2RI8        Opcode + I8 + Rj + Rd
2RI12       Opcode + I12 + Rj + Rd
2RI14       Opcode + I14 + Rj + Rd
2RI16       Opcode + I16 + Rj + Rd
1RI21       Opcode + I21L + Rj + I21H
I26         Opcode + I26L + I26H
=========== ==========================

Opcode是指令操作码，Rj和Rk是源操作数（寄存器），Rd是目标操作数（寄存器），Ra是
4R-type格式特有的附加操作数（寄存器）。I8/I12/I14/I16/I21/I26分别是8位/12位/14位/
16位/21位/26位的立即数。其中较长的21位和26位立即数在指令字中被分割为高位部分与低位
部分，所以你们在这里的格式描述中能够看到I21L/I21H和I26L/I26H这样带后缀的表述。

指令列表
--------

为了简便起见，我们在此只罗列一下指令名称（助记符），需要详细信息请阅读
:ref:`参考文献 <loongarch-references-zh_CN>` 中的文档。

1. 算术运算指令::

    ADD.W SUB.W ADDI.W ADD.D SUB.D ADDI.D
    SLT SLTU SLTI SLTUI
    AND OR NOR XOR ANDN ORN ANDI ORI XORI
    MUL.W MULH.W MULH.WU DIV.W DIV.WU MOD.W MOD.WU
    MUL.D MULH.D MULH.DU DIV.D DIV.DU MOD.D MOD.DU
    PCADDI PCADDU12I PCADDU18I
    LU12I.W LU32I.D LU52I.D ADDU16I.D

2. 移位运算指令::

    SLL.W SRL.W SRA.W ROTR.W SLLI.W SRLI.W SRAI.W ROTRI.W
    SLL.D SRL.D SRA.D ROTR.D SLLI.D SRLI.D SRAI.D ROTRI.D

3. 位域操作指令::

    EXT.W.B EXT.W.H CLO.W CLO.D SLZ.W CLZ.D CTO.W CTO.D CTZ.W CTZ.D
    BYTEPICK.W BYTEPICK.D BSTRINS.W BSTRINS.D BSTRPICK.W BSTRPICK.D
    REVB.2H REVB.4H REVB.2W REVB.D REVH.2W REVH.D BITREV.4B BITREV.8B BITREV.W BITREV.D
    MASKEQZ MASKNEZ

4. 分支转移指令::

    BEQ BNE BLT BGE BLTU BGEU BEQZ BNEZ B BL JIRL

5. 访存读写指令::

    LD.B LD.BU LD.H LD.HU LD.W LD.WU LD.D ST.B ST.H ST.W ST.D
    LDX.B LDX.BU LDX.H LDX.HU LDX.W LDX.WU LDX.D STX.B STX.H STX.W STX.D
    LDPTR.W LDPTR.D STPTR.W STPTR.D
    PRELD PRELDX

6. 原子操作指令::

    LL.W SC.W LL.D SC.D
    AMSWAP.W AMSWAP.D AMADD.W AMADD.D AMAND.W AMAND.D AMOR.W AMOR.D AMXOR.W AMXOR.D
    AMMAX.W AMMAX.D AMMIN.W AMMIN.D

7. 栅障指令::

    IBAR DBAR

8. 特殊指令::

    SYSCALL BREAK CPUCFG NOP IDLE ERTN(ERET) DBCL(DBGCALL) RDTIMEL.W RDTIMEH.W RDTIME.D
    ASRTLE.D ASRTGT.D

9. 特权指令::

    CSRRD CSRWR CSRXCHG
    IOCSRRD.B IOCSRRD.H IOCSRRD.W IOCSRRD.D IOCSRWR.B IOCSRWR.H IOCSRWR.W IOCSRWR.D
    CACOP TLBP(TLBSRCH) TLBRD TLBWR TLBFILL TLBCLR TLBFLUSH INVTLB LDDIR LDPTE

虚拟内存
========

LoongArch可以使用直接映射虚拟内存和分页映射虚拟内存。

直接映射虚拟内存通过CSR.DMWn（n=0~3）来进行配置，虚拟地址（VA）和物理地址（PA）
之间有简单的映射关系::

 VA = PA + 固定偏移

分页映射的虚拟地址（VA）和物理地址（PA）有任意的映射关系，这种关系记录在TLB和页
表中。LoongArch的TLB包括一个全相联的MTLB（Multiple Page Size TLB，多样页大小TLB）
和一个组相联的STLB（Single Page Size TLB，单一页大小TLB）。

缺省状态下，LA32的整个虚拟地址空间配置如下：

============ =========================== ===========================
区段名       地址范围                    属性
============ =========================== ===========================
``UVRANGE``  ``0x00000000 - 0x7FFFFFFF`` 分页映射, 可缓存, PLV0~3
``KPRANGE0`` ``0x80000000 - 0x9FFFFFFF`` 直接映射, 非缓存, PLV0
``KPRANGE1`` ``0xA0000000 - 0xBFFFFFFF`` 直接映射, 可缓存, PLV0
``KVRANGE``  ``0xC0000000 - 0xFFFFFFFF`` 分页映射, 可缓存, PLV0
============ =========================== ===========================

用户态（PLV3）只能访问UVRANGE，对于直接映射的KPRANGE0和KPRANGE1，将虚拟地址的第
30~31位清零就等于物理地址。例如：物理地址0x00001000对应的非缓存直接映射虚拟地址
是0x80001000，而其可缓存直接映射虚拟地址是0xA0001000。

缺省状态下，LA64的整个虚拟地址空间配置如下：

============ ====================== ==================================
区段名       地址范围               属性
============ ====================== ==================================
``XUVRANGE`` ``0x0000000000000000 - 分页映射, 可缓存, PLV0~3
             0x3FFFFFFFFFFFFFFF``
``XSPRANGE`` ``0x4000000000000000 - 直接映射, 可缓存 / 非缓存, PLV0
             0x7FFFFFFFFFFFFFFF``
``XKPRANGE`` ``0x8000000000000000 - 直接映射, 可缓存 / 非缓存, PLV0
             0xBFFFFFFFFFFFFFFF``
``XKVRANGE`` ``0xC000000000000000 - 分页映射, 可缓存, PLV0
             0xFFFFFFFFFFFFFFFF``
============ ====================== ==================================

用户态（PLV3）只能访问XUVRANGE，对于直接映射的XSPRANGE和XKPRANGE，将虚拟地址的第
60~63位清零就等于物理地址，而其缓存属性是通过虚拟地址的第60~61位配置的（0表示强序
非缓存，1表示一致可缓存，2表示弱序非缓存）。

目前，我们仅用XKPRANGE来进行直接映射，XSPRANGE保留给以后用。

此处给出一个直接映射的例子：物理地址0x00000000_00001000的强序非缓存直接映射虚拟地址
（在XKPRANGE中）是0x80000000_00001000，其一致可缓存直接映射虚拟地址（在XKPRANGE中）
是0x90000000_00001000，而其弱序非缓存直接映射虚拟地址（在XKPRANGE中）是0xA0000000_
00001000。

Loongson与LoongArch的关系
=========================

LoongArch是一种RISC指令集架构（ISA），不同于现存的任何一种ISA，而Loongson（即龙
芯）是一个处理器家族。龙芯包括三个系列：Loongson-1（龙芯1号）是32位处理器系列，
Loongson-2（龙芯2号）是低端64位处理器系列，而Loongson-3（龙芯3号）是高端64位处理
器系列。旧的龙芯处理器基于MIPS架构，而新的龙芯处理器基于LoongArch架构。以龙芯3号
为例：龙芯3A1000/3B1500/3A2000/3A3000/3A4000都是兼容MIPS的，而龙芯3A5000（以及将
来的型号）都是基于LoongArch的。

.. _loongarch-references-zh_CN:

参考文献
========

Loongson官方网站（龙芯中科技术股份有限公司）：

  http://www.loongson.cn/

Loongson与LoongArch的开发者网站（软件与文档资源）：

  http://www.loongnix.cn/

  https://github.com/loongson/

  https://loongson.github.io/LoongArch-Documentation/

LoongArch指令集架构的文档：

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-Vol1-v1.00-CN.pdf （中文版）

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-Vol1-v1.00-EN.pdf （英文版）

LoongArch的ELF psABI文档：

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-ELF-ABI-v1.00-CN.pdf （中文版）

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-ELF-ABI-v1.00-EN.pdf （英文版）

Loongson与LoongArch的Linux内核源码仓库：

  https://git.kernel.org/pub/scm/linux/kernel/git/chenhuacai/linux-loongson.git
