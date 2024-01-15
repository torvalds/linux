.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_TW.rst

:Original: Documentation/arch/loongarch/introduction.rst
:Translator: Huacai Chen <chenhuacai@loongson.cn>

=============
LoongArch介紹
=============

LoongArch是一種新的RISC ISA，在一定程度上類似於MIPS和RISC-V。LoongArch指令集
包括一個精簡32位版（LA32R）、一個標準32位版（LA32S）、一個64位版（LA64）。
LoongArch定義了四個特權級（PLV0~PLV3），其中PLV0是最高特權級，用於內核；而PLV3
是最低特權級，用於應用程序。本文檔介紹了LoongArch的寄存器、基礎指令集、虛擬內
存以及其他一些主題。

寄存器
======

LoongArch的寄存器包括通用寄存器（GPRs）、浮點寄存器（FPRs）、向量寄存器（VRs）
和用於特權模式（PLV0）的控制狀態寄存器（CSRs）。

通用寄存器
----------

LoongArch包括32個通用寄存器（ ``$r0`` ~ ``$r31`` ），LA32中每個寄存器爲32位寬，
LA64中每個寄存器爲64位寬。 ``$r0`` 的內容總是固定爲0，而其他寄存器在體系結構層面
沒有特殊功能。（ ``$r1`` 算是一個例外，在BL指令中固定用作鏈接返回寄存器。）

內核使用了一套LoongArch寄存器約定，定義在LoongArch ELF psABI規範中，詳細描述參見
:ref:`參考文獻 <loongarch-references-zh_TW>`:

================= =============== =================== ==========
寄存器名          別名            用途                跨調用保持
================= =============== =================== ==========
``$r0``           ``$zero``       常量0               不使用
``$r1``           ``$ra``         返回地址            否
``$r2``           ``$tp``         TLS/線程信息指針    不使用
``$r3``           ``$sp``         棧指針              是
``$r4``-``$r11``  ``$a0``-``$a7`` 參數寄存器          否
``$r4``-``$r5``   ``$v0``-``$v1`` 返回值              否
``$r12``-``$r20`` ``$t0``-``$t8`` 臨時寄存器          否
``$r21``          ``$u0``         每CPU變量基地址     不使用
``$r22``          ``$fp``         幀指針              是
``$r23``-``$r31`` ``$s0``-``$s8`` 靜態寄存器          是
================= =============== =================== ==========

.. note::
    注意： ``$r21`` 寄存器在ELF psABI中保留未使用，但是在Linux內核用於保
    存每CPU變量基地址。該寄存器沒有ABI命名，不過在內核中稱爲 ``$u0`` 。在
    一些遺留代碼中有時可能見到 ``$v0`` 和 ``$v1`` ，它們是 ``$a0`` 和
    ``$a1`` 的別名，屬於已經廢棄的用法。

浮點寄存器
----------

當系統中存在FPU時，LoongArch有32個浮點寄存器（ ``$f0`` ~ ``$f31`` ）。在LA64
的CPU核上，每個寄存器均爲64位寬。

浮點寄存器的使用約定與LoongArch ELF psABI規範的描述相同：

================= ================== =================== ==========
寄存器名          別名               用途                跨調用保持
================= ================== =================== ==========
``$f0``-``$f7``   ``$fa0``-``$fa7``  參數寄存器          否
``$f0``-``$f1``   ``$fv0``-``$fv1``  返回值              否
``$f8``-``$f23``  ``$ft0``-``$ft15`` 臨時寄存器          否
``$f24``-``$f31`` ``$fs0``-``$fs7``  靜態寄存器          是
================= ================== =================== ==========

.. note::
    注意：在一些遺留代碼中有時可能見到 ``$fv0`` 和 ``$fv1`` ，它們是
    ``$fa0`` 和 ``$fa1`` 的別名，屬於已經廢棄的用法。


向量寄存器
----------

LoongArch現有兩種向量擴展：

- 128位向量擴展LSX（全稱Loongson SIMD eXtention），
- 256位向量擴展LASX（全稱Loongson Advanced SIMD eXtention）。

LSX使用 ``$v0`` ~ ``$v31`` 向量寄存器，而LASX則使用 ``$x0`` ~ ``$x31`` 。

浮點寄存器和向量寄存器是複用的，比如：在一個實現了LSX和LASX的核上， ``$x0`` 的
低128位與 ``$v0`` 共用， ``$v0`` 的低64位與 ``$f0`` 共用，其他寄存器依此類推。

控制狀態寄存器
--------------

控制狀態寄存器只能在特權模式（PLV0）下訪問:

================= ==================================== ==========
地址              全稱描述                             簡稱
================= ==================================== ==========
0x0               當前模式信息                         CRMD
0x1               異常前模式信息                       PRMD
0x2               擴展部件使能                         EUEN
0x3               雜項控制                             MISC
0x4               異常配置                             ECFG
0x5               異常狀態                             ESTAT
0x6               異常返回地址                         ERA
0x7               出錯(Faulting)虛擬地址               BADV
0x8               出錯(Faulting)指令字                 BADI
0xC               異常入口地址                         EENTRY
0x10              TLB索引                              TLBIDX
0x11              TLB表項高位                          TLBEHI
0x12              TLB表項低位0                         TLBELO0
0x13              TLB表項低位1                         TLBELO1
0x18              地址空間標識符                       ASID
0x19              低半地址空間頁全局目錄基址           PGDL
0x1A              高半地址空間頁全局目錄基址           PGDH
0x1B              頁全局目錄基址                       PGD
0x1C              頁表遍歷控制低半部分                 PWCL
0x1D              頁表遍歷控制高半部分                 PWCH
0x1E              STLB頁大小                           STLBPS
0x1F              縮減虛地址配置                       RVACFG
0x20              CPU編號                              CPUID
0x21              特權資源配置信息1                    PRCFG1
0x22              特權資源配置信息2                    PRCFG2
0x23              特權資源配置信息3                    PRCFG3
0x30+n (0≤n≤15)   數據保存寄存器                       SAVEn
0x40              定時器編號                           TID
0x41              定時器配置                           TCFG
0x42              定時器值                             TVAL
0x43              計時器補償                           CNTC
0x44              定時器中斷清除                       TICLR
0x60              LLBit相關控制                        LLBCTL
0x80              實現相關控制1                        IMPCTL1
0x81              實現相關控制2                        IMPCTL2
0x88              TLB重填異常入口地址                  TLBRENTRY
0x89              TLB重填異常出錯(Faulting)虛地址      TLBRBADV
0x8A              TLB重填異常返回地址                  TLBRERA
0x8B              TLB重填異常數據保存                  TLBRSAVE
0x8C              TLB重填異常表項低位0                 TLBRELO0
0x8D              TLB重填異常表項低位1                 TLBRELO1
0x8E              TLB重填異常表項高位                  TLBEHI
0x8F              TLB重填異常前模式信息                TLBRPRMD
0x90              機器錯誤控制                         MERRCTL
0x91              機器錯誤信息1                        MERRINFO1
0x92              機器錯誤信息2                        MERRINFO2
0x93              機器錯誤異常入口地址                 MERRENTRY
0x94              機器錯誤異常返回地址                 MERRERA
0x95              機器錯誤異常數據保存                 MERRSAVE
0x98              高速緩存標籤                         CTAG
0x180+n (0≤n≤3)   直接映射配置窗口n                    DMWn
0x200+2n (0≤n≤31) 性能監測配置n                        PMCFGn
0x201+2n (0≤n≤31) 性能監測計數器n                      PMCNTn
0x300             內存讀寫監視點整體控制               MWPC
0x301             內存讀寫監視點整體狀態               MWPS
0x310+8n (0≤n≤7)  內存讀寫監視點n配置1                 MWPnCFG1
0x311+8n (0≤n≤7)  內存讀寫監視點n配置2                 MWPnCFG2
0x312+8n (0≤n≤7)  內存讀寫監視點n配置3                 MWPnCFG3
0x313+8n (0≤n≤7)  內存讀寫監視點n配置4                 MWPnCFG4
0x380             取指監視點整體控制                   FWPC
0x381             取指監視點整體狀態                   FWPS
0x390+8n (0≤n≤7)  取指監視點n配置1                     FWPnCFG1
0x391+8n (0≤n≤7)  取指監視點n配置2                     FWPnCFG2
0x392+8n (0≤n≤7)  取指監視點n配置3                     FWPnCFG3
0x393+8n (0≤n≤7)  取指監視點n配置4                     FWPnCFG4
0x500             調試寄存器                           DBG
0x501             調試異常返回地址                     DERA
0x502             調試數據保存                         DSAVE
================= ==================================== ==========

ERA，TLBRERA，MERRERA和DERA有時也分別稱爲EPC，TLBREPC，MERREPC和DEPC。

基礎指令集
==========

指令格式
--------

LoongArch的指令字長爲32位，一共有9種基本指令格式（以及一些變體）:

=========== ==========================
格式名稱    指令構成
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

Opcode是指令操作碼，Rj和Rk是源操作數（寄存器），Rd是目標操作數（寄存器），Ra是
4R-type格式特有的附加操作數（寄存器）。I8/I12/I14/I16/I21/I26分別是8位/12位/14位/
16位/21位/26位的立即數。其中較長的21位和26位立即數在指令字中被分割爲高位部分與低位
部分，所以你們在這裏的格式描述中能夠看到I21L/I21H和I26L/I26H這樣帶後綴的表述。

指令列表
--------

爲了簡便起見，我們在此只羅列一下指令名稱（助記符），需要詳細信息請閱讀
:ref:`參考文獻 <loongarch-references-zh_TW>` 中的文檔。

1. 算術運算指令::

    ADD.W SUB.W ADDI.W ADD.D SUB.D ADDI.D
    SLT SLTU SLTI SLTUI
    AND OR NOR XOR ANDN ORN ANDI ORI XORI
    MUL.W MULH.W MULH.WU DIV.W DIV.WU MOD.W MOD.WU
    MUL.D MULH.D MULH.DU DIV.D DIV.DU MOD.D MOD.DU
    PCADDI PCADDU12I PCADDU18I
    LU12I.W LU32I.D LU52I.D ADDU16I.D

2. 移位運算指令::

    SLL.W SRL.W SRA.W ROTR.W SLLI.W SRLI.W SRAI.W ROTRI.W
    SLL.D SRL.D SRA.D ROTR.D SLLI.D SRLI.D SRAI.D ROTRI.D

3. 位域操作指令::

    EXT.W.B EXT.W.H CLO.W CLO.D SLZ.W CLZ.D CTO.W CTO.D CTZ.W CTZ.D
    BYTEPICK.W BYTEPICK.D BSTRINS.W BSTRINS.D BSTRPICK.W BSTRPICK.D
    REVB.2H REVB.4H REVB.2W REVB.D REVH.2W REVH.D BITREV.4B BITREV.8B BITREV.W BITREV.D
    MASKEQZ MASKNEZ

4. 分支轉移指令::

    BEQ BNE BLT BGE BLTU BGEU BEQZ BNEZ B BL JIRL

5. 訪存讀寫指令::

    LD.B LD.BU LD.H LD.HU LD.W LD.WU LD.D ST.B ST.H ST.W ST.D
    LDX.B LDX.BU LDX.H LDX.HU LDX.W LDX.WU LDX.D STX.B STX.H STX.W STX.D
    LDPTR.W LDPTR.D STPTR.W STPTR.D
    PRELD PRELDX

6. 原子操作指令::

    LL.W SC.W LL.D SC.D
    AMSWAP.W AMSWAP.D AMADD.W AMADD.D AMAND.W AMAND.D AMOR.W AMOR.D AMXOR.W AMXOR.D
    AMMAX.W AMMAX.D AMMIN.W AMMIN.D

7. 柵障指令::

    IBAR DBAR

8. 特殊指令::

    SYSCALL BREAK CPUCFG NOP IDLE ERTN(ERET) DBCL(DBGCALL) RDTIMEL.W RDTIMEH.W RDTIME.D
    ASRTLE.D ASRTGT.D

9. 特權指令::

    CSRRD CSRWR CSRXCHG
    IOCSRRD.B IOCSRRD.H IOCSRRD.W IOCSRRD.D IOCSRWR.B IOCSRWR.H IOCSRWR.W IOCSRWR.D
    CACOP TLBP(TLBSRCH) TLBRD TLBWR TLBFILL TLBCLR TLBFLUSH INVTLB LDDIR LDPTE

虛擬內存
========

LoongArch可以使用直接映射虛擬內存和分頁映射虛擬內存。

直接映射虛擬內存通過CSR.DMWn（n=0~3）來進行配置，虛擬地址（VA）和物理地址（PA）
之間有簡單的映射關係::

 VA = PA + 固定偏移

分頁映射的虛擬地址（VA）和物理地址（PA）有任意的映射關係，這種關係記錄在TLB和頁
表中。LoongArch的TLB包括一個全相聯的MTLB（Multiple Page Size TLB，多樣頁大小TLB）
和一個組相聯的STLB（Single Page Size TLB，單一頁大小TLB）。

缺省狀態下，LA32的整個虛擬地址空間配置如下：

============ =========================== ===========================
區段名       地址範圍                    屬性
============ =========================== ===========================
``UVRANGE``  ``0x00000000 - 0x7FFFFFFF`` 分頁映射, 可緩存, PLV0~3
``KPRANGE0`` ``0x80000000 - 0x9FFFFFFF`` 直接映射, 非緩存, PLV0
``KPRANGE1`` ``0xA0000000 - 0xBFFFFFFF`` 直接映射, 可緩存, PLV0
``KVRANGE``  ``0xC0000000 - 0xFFFFFFFF`` 分頁映射, 可緩存, PLV0
============ =========================== ===========================

用戶態（PLV3）只能訪問UVRANGE，對於直接映射的KPRANGE0和KPRANGE1，將虛擬地址的第
30~31位清零就等於物理地址。例如：物理地址0x00001000對應的非緩存直接映射虛擬地址
是0x80001000，而其可緩存直接映射虛擬地址是0xA0001000。

缺省狀態下，LA64的整個虛擬地址空間配置如下：

============ ====================== ==================================
區段名       地址範圍               屬性
============ ====================== ==================================
``XUVRANGE`` ``0x0000000000000000 - 分頁映射, 可緩存, PLV0~3
             0x3FFFFFFFFFFFFFFF``
``XSPRANGE`` ``0x4000000000000000 - 直接映射, 可緩存 / 非緩存, PLV0
             0x7FFFFFFFFFFFFFFF``
``XKPRANGE`` ``0x8000000000000000 - 直接映射, 可緩存 / 非緩存, PLV0
             0xBFFFFFFFFFFFFFFF``
``XKVRANGE`` ``0xC000000000000000 - 分頁映射, 可緩存, PLV0
             0xFFFFFFFFFFFFFFFF``
============ ====================== ==================================

用戶態（PLV3）只能訪問XUVRANGE，對於直接映射的XSPRANGE和XKPRANGE，將虛擬地址的第
60~63位清零就等於物理地址，而其緩存屬性是通過虛擬地址的第60~61位配置的（0表示強序
非緩存，1表示一致可緩存，2表示弱序非緩存）。

目前，我們僅用XKPRANGE來進行直接映射，XSPRANGE保留給以後用。

此處給出一個直接映射的例子：物理地址0x00000000_00001000的強序非緩存直接映射虛擬地址
（在XKPRANGE中）是0x80000000_00001000，其一致可緩存直接映射虛擬地址（在XKPRANGE中）
是0x90000000_00001000，而其弱序非緩存直接映射虛擬地址（在XKPRANGE中）是0xA0000000_
00001000。

Loongson與LoongArch的關係
=========================

LoongArch是一種RISC指令集架構（ISA），不同於現存的任何一種ISA，而Loongson（即龍
芯）是一個處理器家族。龍芯包括三個系列：Loongson-1（龍芯1號）是32位處理器系列，
Loongson-2（龍芯2號）是低端64位處理器系列，而Loongson-3（龍芯3號）是高端64位處理
器系列。舊的龍芯處理器基於MIPS架構，而新的龍芯處理器基於LoongArch架構。以龍芯3號
爲例：龍芯3A1000/3B1500/3A2000/3A3000/3A4000都是兼容MIPS的，而龍芯3A5000（以及將
來的型號）都是基於LoongArch的。

.. _loongarch-references-zh_TW:

參考文獻
========

Loongson官方網站（龍芯中科技術股份有限公司）：

  http://www.loongson.cn/

Loongson與LoongArch的開發者網站（軟件與文檔資源）：

  http://www.loongnix.cn/

  https://github.com/loongson/

  https://loongson.github.io/LoongArch-Documentation/

LoongArch指令集架構的文檔：

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-Vol1-v1.02-CN.pdf （中文版）

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-Vol1-v1.02-EN.pdf （英文版）

LoongArch的ELF psABI文檔：

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-ELF-ABI-v2.01-CN.pdf （中文版）

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/LoongArch-ELF-ABI-v2.01-EN.pdf （英文版）

Loongson與LoongArch的Linux內核源碼倉庫：

  https://git.kernel.org/pub/scm/linux/kernel/git/chenhuacai/linux-loongson.git

