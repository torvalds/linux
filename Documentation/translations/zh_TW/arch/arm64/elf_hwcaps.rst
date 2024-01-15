.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/arch/arm64/elf_hwcaps.rst <elf_hwcaps_index>`

Translator: Bailu Lin <bailu.lin@vivo.com>
            Hu Haowen <src.res.211@gmail.com>

================
ARM64 ELF hwcaps
================

這篇文檔描述了 arm64 ELF hwcaps 的用法和語義。


1. 簡介
-------

有些硬件或軟件功能僅在某些 CPU 實現上和/或在具體某個內核配置上可用，但
對於處於 EL0 的用戶空間代碼沒有可用的架構發現機制。內核通過在輔助向量表
公開一組稱爲 hwcaps 的標誌而把這些功能暴露給用戶空間。

用戶空間軟件可以通過獲取輔助向量的 AT_HWCAP 或 AT_HWCAP2 條目來測試功能，
並測試是否設置了相關標誌，例如::

	bool floating_point_is_present(void)
	{
		unsigned long hwcaps = getauxval(AT_HWCAP);
		if (hwcaps & HWCAP_FP)
			return true;

		return false;
	}

如果軟件依賴於 hwcap 描述的功能，在嘗試使用該功能前則應檢查相關的 hwcap
標誌以驗證該功能是否存在。

不能通過其他方式探查這些功能。當一個功能不可用時，嘗試使用它可能導致不可
預測的行爲，並且無法保證能確切的知道該功能不可用，例如 SIGILL。


2. Hwcaps 的說明
----------------

大多數 hwcaps 旨在說明通過架構 ID 寄存器(處於 EL0 的用戶空間代碼無法訪問)
描述的功能的存在。這些 hwcap 通過 ID 寄存器字段定義，並且應根據 ARM 體系
結構參考手冊（ARM ARM）中定義的字段來解釋說明。

這些 hwcaps 以下面的形式描述::

    idreg.field == val 表示有某個功能。

當 idreg.field 中有 val 時，hwcaps 表示 ARM ARM 定義的功能是有效的，但是
並不是說要完全和 val 相等，也不是說 idreg.field 描述的其他功能就是缺失的。

其他 hwcaps 可能表明無法僅由 ID 寄存器描述的功能的存在。這些 hwcaps 可能
沒有被 ID 寄存器描述，需要參考其他文檔。


3. AT_HWCAP 中揭示的 hwcaps
---------------------------

HWCAP_FP
    ID_AA64PFR0_EL1.FP == 0b0000 表示有此功能。

HWCAP_ASIMD
    ID_AA64PFR0_EL1.AdvSIMD == 0b0000 表示有此功能。

HWCAP_EVTSTRM
    通用計時器頻率配置爲大約100KHz以生成事件。

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
    根據 Documentation/arch/arm64/cpu-feature-registers.rst 描述，EL0 可以訪問
    某些 ID 寄存器。

    這些 ID 寄存器可能表示功能的可用性。

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
    如 Documentation/arch/arm64/pointer-authentication.rst 所描述，
    ID_AA64ISAR1_EL1.APA == 0b0001 或 ID_AA64ISAR1_EL1.API == 0b0001
    表示有此功能。

HWCAP_PACG
    如 Documentation/arch/arm64/pointer-authentication.rst 所描述，
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

爲了與用戶空間交互，內核保證 AT_HWCAP 的第62、63位將始終返回0。

