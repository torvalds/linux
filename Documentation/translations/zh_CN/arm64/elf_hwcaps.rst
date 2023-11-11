.. include:: ../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/arm64/elf_hwcaps.rst <elf_hwcaps_index>`

Translator: Bailu Lin <bailu.lin@vivo.com>

================
ARM64 ELF hwcaps
================

这篇文档描述了 arm64 ELF hwcaps 的用法和语义。


1. 简介
-------

有些硬件或软件功能仅在某些 CPU 实现上和/或在具体某个内核配置上可用，但
对于处于 EL0 的用户空间代码没有可用的架构发现机制。内核通过在辅助向量表
公开一组称为 hwcaps 的标志而把这些功能暴露给用户空间。

用户空间软件可以通过获取辅助向量的 AT_HWCAP 或 AT_HWCAP2 条目来测试功能，
并测试是否设置了相关标志，例如::

	bool floating_point_is_present(void)
	{
		unsigned long hwcaps = getauxval(AT_HWCAP);
		if (hwcaps & HWCAP_FP)
			return true;

		return false;
	}

如果软件依赖于 hwcap 描述的功能，在尝试使用该功能前则应检查相关的 hwcap
标志以验证该功能是否存在。

不能通过其他方式探查这些功能。当一个功能不可用时，尝试使用它可能导致不可
预测的行为，并且无法保证能确切的知道该功能不可用，例如 SIGILL。


2. Hwcaps 的说明
----------------

大多数 hwcaps 旨在说明通过架构 ID 寄存器(处于 EL0 的用户空间代码无法访问)
描述的功能的存在。这些 hwcap 通过 ID 寄存器字段定义，并且应根据 ARM 体系
结构参考手册（ARM ARM）中定义的字段来解释说明。

这些 hwcaps 以下面的形式描述::

    idreg.field == val 表示有某个功能。

当 idreg.field 中有 val 时，hwcaps 表示 ARM ARM 定义的功能是有效的，但是
并不是说要完全和 val 相等，也不是说 idreg.field 描述的其他功能就是缺失的。

其他 hwcaps 可能表明无法仅由 ID 寄存器描述的功能的存在。这些 hwcaps 可能
没有被 ID 寄存器描述，需要参考其他文档。


3. AT_HWCAP 中揭示的 hwcaps
---------------------------

HWCAP_FP
    ID_AA64PFR0_EL1.FP == 0b0000 表示有此功能。

HWCAP_ASIMD
    ID_AA64PFR0_EL1.AdvSIMD == 0b0000 表示有此功能。

HWCAP_EVTSTRM
    通用计时器频率配置为大约100KHz以生成事件。

HWCAP_AES
    ID_AA64ISAR0_EL1.AES == 0b0001 表示有此功能。

HWCAP_PMULL
    ID_AA64ISAR0_EL1.AES == 0b0010 表示有此功能。

HWCAP_SHA1
    ID_AA64ISAR0_EL1.SHA1 == 0b0001 表示有此功能。

HWCAP_SHA2
    ID_AA64ISAR0_EL1.SHA2 == 0b0001 表示有此功能。

HWCAP_CRC32
    ID_AA64ISAR0_EL1.CRC32 == 0b0001 表示有此功能。

HWCAP_ATOMICS
    ID_AA64ISAR0_EL1.Atomic == 0b0010 表示有此功能。

HWCAP_FPHP
    ID_AA64PFR0_EL1.FP == 0b0001 表示有此功能。

HWCAP_ASIMDHP
    ID_AA64PFR0_EL1.AdvSIMD == 0b0001 表示有此功能。

HWCAP_CPUID
    根据 Documentation/arm64/cpu-feature-registers.rst 描述，EL0 可以访问
    某些 ID 寄存器。

    这些 ID 寄存器可能表示功能的可用性。

HWCAP_ASIMDRDM
    ID_AA64ISAR0_EL1.RDM == 0b0001 表示有此功能。

HWCAP_JSCVT
    ID_AA64ISAR1_EL1.JSCVT == 0b0001 表示有此功能。

HWCAP_FCMA
    ID_AA64ISAR1_EL1.FCMA == 0b0001 表示有此功能。

HWCAP_LRCPC
    ID_AA64ISAR1_EL1.LRCPC == 0b0001 表示有此功能。

HWCAP_DCPOP
    ID_AA64ISAR1_EL1.DPB == 0b0001 表示有此功能。

HWCAP_SHA3
    ID_AA64ISAR0_EL1.SHA3 == 0b0001 表示有此功能。

HWCAP_SM3
    ID_AA64ISAR0_EL1.SM3 == 0b0001 表示有此功能。

HWCAP_SM4
    ID_AA64ISAR0_EL1.SM4 == 0b0001 表示有此功能。

HWCAP_ASIMDDP
    ID_AA64ISAR0_EL1.DP == 0b0001 表示有此功能。

HWCAP_SHA512
    ID_AA64ISAR0_EL1.SHA2 == 0b0010 表示有此功能。

HWCAP_SVE
    ID_AA64PFR0_EL1.SVE == 0b0001 表示有此功能。

HWCAP_ASIMDFHM
    ID_AA64ISAR0_EL1.FHM == 0b0001 表示有此功能。

HWCAP_DIT
    ID_AA64PFR0_EL1.DIT == 0b0001 表示有此功能。

HWCAP_USCAT
    ID_AA64MMFR2_EL1.AT == 0b0001 表示有此功能。

HWCAP_ILRCPC
    ID_AA64ISAR1_EL1.LRCPC == 0b0010 表示有此功能。

HWCAP_FLAGM
    ID_AA64ISAR0_EL1.TS == 0b0001 表示有此功能。

HWCAP_SSBS
    ID_AA64PFR1_EL1.SSBS == 0b0010 表示有此功能。

HWCAP_SB
    ID_AA64ISAR1_EL1.SB == 0b0001 表示有此功能。

HWCAP_PACA
    如 Documentation/arm64/pointer-authentication.rst 所描述，
    ID_AA64ISAR1_EL1.APA == 0b0001 或 ID_AA64ISAR1_EL1.API == 0b0001
    表示有此功能。

HWCAP_PACG
    如 Documentation/arm64/pointer-authentication.rst 所描述，
    ID_AA64ISAR1_EL1.GPA == 0b0001 或 ID_AA64ISAR1_EL1.GPI == 0b0001
    表示有此功能。

HWCAP2_DCPODP

    ID_AA64ISAR1_EL1.DPB == 0b0010 表示有此功能。

HWCAP2_SVE2

    ID_AA64ZFR0_EL1.SVEVer == 0b0001 表示有此功能。

HWCAP2_SVEAES

    ID_AA64ZFR0_EL1.AES == 0b0001 表示有此功能。

HWCAP2_SVEPMULL

    ID_AA64ZFR0_EL1.AES == 0b0010 表示有此功能。

HWCAP2_SVEBITPERM

    ID_AA64ZFR0_EL1.BitPerm == 0b0001 表示有此功能。

HWCAP2_SVESHA3

    ID_AA64ZFR0_EL1.SHA3 == 0b0001 表示有此功能。

HWCAP2_SVESM4

    ID_AA64ZFR0_EL1.SM4 == 0b0001 表示有此功能。

HWCAP2_FLAGM2

    ID_AA64ISAR0_EL1.TS == 0b0010 表示有此功能。

HWCAP2_FRINT

    ID_AA64ISAR1_EL1.FRINTTS == 0b0001 表示有此功能。

HWCAP2_SVEI8MM

    ID_AA64ZFR0_EL1.I8MM == 0b0001 表示有此功能。

HWCAP2_SVEF32MM

    ID_AA64ZFR0_EL1.F32MM == 0b0001 表示有此功能。

HWCAP2_SVEF64MM

    ID_AA64ZFR0_EL1.F64MM == 0b0001 表示有此功能。

HWCAP2_SVEBF16

    ID_AA64ZFR0_EL1.BF16 == 0b0001 表示有此功能。

HWCAP2_I8MM

    ID_AA64ISAR1_EL1.I8MM == 0b0001 表示有此功能。

HWCAP2_BF16

    ID_AA64ISAR1_EL1.BF16 == 0b0001 表示有此功能。

HWCAP2_DGH

    ID_AA64ISAR1_EL1.DGH == 0b0001 表示有此功能。

HWCAP2_RNG

    ID_AA64ISAR0_EL1.RNDR == 0b0001 表示有此功能。

HWCAP2_BTI

    ID_AA64PFR0_EL1.BT == 0b0001 表示有此功能。


4. 未使用的 AT_HWCAP 位
-----------------------

为了与用户空间交互，内核保证 AT_HWCAP 的第62、63位将始终返回0。
