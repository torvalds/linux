obj-y := time.o cpu.o system.o irq-common.o common.o

obj-$(CONFIG_SOC_IMX1) += clk-imx1.o mm-imx1.o
obj-$(CONFIG_SOC_IMX21) += clk-imx21.o mm-imx21.o

obj-$(CONFIG_SOC_IMX25) += clk-imx25.o cpu-imx25.o mach-imx25.o

obj-$(CONFIG_SOC_IMX27) += cpu-imx27.o pm-imx27.o
obj-$(CONFIG_SOC_IMX27) += clk-imx27.o mm-imx27.o ehci-imx27.o

obj-$(CONFIG_SOC_IMX31) += mm-imx3.o cpu-imx31.o clk-imx31.o iomux-imx31.o ehci-imx31.o pm-imx3.o
obj-$(CONFIG_SOC_IMX35) += mm-imx3.o cpu-imx35.o clk-imx35.o ehci-imx35.o pm-imx3.o

imx5-pm-$(CONFIG_PM) += pm-imx5.o
obj-$(CONFIG_SOC_IMX5) += cpu-imx5.o clk-imx51-imx53.o clk-cpu.o $(imx5-pm-y)

obj-$(CONFIG_COMMON_CLK) += clk-pllv1.o clk-pllv2.o clk-pllv3.o clk-gate2.o \
			    clk-pfd.o clk-busy.o clk.o \
			    clk-fixup-div.o clk-fixup-mux.o \
			    clk-gate-exclusive.o

obj-$(CONFIG_IMX_HAVE_IOMUX_V1) += iomux-v1.o
obj-$(CONFIG_ARCH_MXC_IOMUX_V3) += iomux-v3.o

obj-$(CONFIG_MXC_TZIC) += tzic.o
obj-$(CONFIG_MXC_AVIC) += avic.o

obj-$(CONFIG_MXC_USE_EPIT) += epit.o
obj-$(CONFIG_MXC_DEBUG_BOARD) += 3ds_debugboard.o

ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_SOC_IMX5) += cpuidle-imx5.o
obj-$(CONFIG_SOC_IMX6Q) += cpuidle-imx6q.o
AFLAGS_imx6sl_low_power_idle.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6SL) += cpuidle-imx6sl.o imx6sl_low_power_idle.o
AFLAGS_imx6sx_low_power_idle.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6SX) += cpuidle-imx6sx.o imx6sx_low_power_idle.o
AFLAGS_imx6ul_low_power_idle.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6UL) += cpuidle-imx6ul.o imx6ul_low_power_idle.o
AFLAGS_imx6ull_low_power_idle.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6ULL) += imx6ull_low_power_idle.o
AFLAGS_imx7d_low_power_idle.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX7D) += cpuidle-imx7d.o imx7d_low_power_idle.o
endif

ifdef CONFIG_SND_IMX_SOC
obj-y += ssi-fiq.o
obj-y += ssi-fiq-ksym.o
endif

# i.MX1 based machines
obj-$(CONFIG_MACH_SCB9328) += mach-scb9328.o
obj-$(CONFIG_MACH_APF9328) += mach-apf9328.o
obj-$(CONFIG_MACH_IMX1_DT) += imx1-dt.o

# i.MX21 based machines
obj-$(CONFIG_MACH_MX21ADS) += mach-mx21ads.o

# i.MX27 based machines
obj-$(CONFIG_MACH_MX27ADS) += mach-mx27ads.o
obj-$(CONFIG_MACH_MX27_3DS) += mach-mx27_3ds.o
obj-$(CONFIG_MACH_IMX27_VISSTRIM_M10) += mach-imx27_visstrim_m10.o
obj-$(CONFIG_MACH_PCA100) += mach-pca100.o
obj-$(CONFIG_MACH_IMX27_DT) += imx27-dt.o

# i.MX31 based machines
obj-$(CONFIG_MACH_MX31ADS) += mach-mx31ads.o
obj-$(CONFIG_MACH_MX31LILLY) += mach-mx31lilly.o mx31lilly-db.o
obj-$(CONFIG_MACH_MX31LITE) += mach-mx31lite.o mx31lite-db.o
obj-$(CONFIG_MACH_PCM037) += mach-pcm037.o
obj-$(CONFIG_MACH_PCM037_EET) += mach-pcm037_eet.o
obj-$(CONFIG_MACH_MX31_3DS) += mach-mx31_3ds.o
obj-$(CONFIG_MACH_MX31MOBOARD) += mach-mx31moboard.o mx31moboard-devboard.o \
		mx31moboard-marxbot.o mx31moboard-smartbot.o
obj-$(CONFIG_MACH_QONG) += mach-qong.o
obj-$(CONFIG_MACH_ARMADILLO5X0) += mach-armadillo5x0.o
obj-$(CONFIG_MACH_KZM_ARM11_01) += mach-kzm_arm11_01.o
obj-$(CONFIG_MACH_BUG) += mach-bug.o
obj-$(CONFIG_MACH_IMX31_DT) += imx31-dt.o

# i.MX35 based machines
obj-$(CONFIG_MACH_PCM043) += mach-pcm043.o
obj-$(CONFIG_MACH_MX35_3DS) += mach-mx35_3ds.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX35SD) += mach-cpuimx35.o
obj-$(CONFIG_MACH_EUKREA_MBIMXSD35_BASEBOARD) += eukrea_mbimxsd35-baseboard.o
obj-$(CONFIG_MACH_VPR200) += mach-vpr200.o
obj-$(CONFIG_MACH_IMX35_DT) += imx35-dt.o

obj-$(CONFIG_HAVE_IMX_ANATOP) += anatop.o
obj-$(CONFIG_HAVE_IMX_GPC) += gpc.o
obj-$(CONFIG_HAVE_IMX_GPCV2) += gpcv2.o
obj-$(CONFIG_HAVE_IMX_MMDC) += mmdc.o
obj-$(CONFIG_HAVE_IMX_DDRC) += ddrc.o
obj-$(CONFIG_HAVE_IMX_SRC) += src.o
obj-$(CONFIG_HAVE_IMX_MU) += mu.o
obj-$(CONFIG_HAVE_IMX_RPMSG) += imx_rpmsg.o
ifneq ($(CONFIG_SOC_IMX6)$(CONFIG_SOC_IMX7)$(CONFIG_SOC_LS1021A),)
AFLAGS_headsmp.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SMP) += headsmp.o platsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += hotplug.o
endif
obj-y += busfreq_lpddr2.o busfreq-imx.o busfreq_ddr3.o
AFLAGS_ddr3_freq_imx6.o :=-Wa,-march=armv7-a
AFLAGS_smp_wfe_imx6.o :=-Wa,-march=armv7-a
AFLAGS_lpddr2_freq_imx6q.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6Q) += clk-imx6q.o mach-imx6q.o ddr3_freq_imx6.o \
			smp_wfe_imx6.o lpddr2_freq_imx6q.o
AFLAGS_lpddr2_freq_imx6.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6SL) += clk-imx6sl.o mach-imx6sl.o lpddr2_freq_imx6.o
AFLAGS_ddr3_freq_imx6sx.o :=-Wa,-march=armv7-a
AFLAGS_lpddr2_freq_imx6sx.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6SX) += clk-imx6sx.o mach-imx6sx.o ddr3_freq_imx6sx.o \
			    lpddr2_freq_imx6sx.o
obj-$(CONFIG_SOC_IMX6UL) += clk-imx6ul.o mach-imx6ul.o ddr3_freq_imx6sx.o \
			    lpddr2_freq_imx6sx.o
obj-$(CONFIG_SOC_IMX7D) += clk-imx7d.o mach-imx7d.o

ifeq ($(CONFIG_SUSPEND),y)
AFLAGS_suspend-imx6.o :=-Wa,-march=armv7-a
AFLAGS_suspend-imx7.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6) += suspend-imx6.o
obj-$(CONFIG_SOC_IMX7) += suspend-imx7.o
endif
obj-$(CONFIG_SOC_IMX6) += pm-imx6.o
AFLAGS_smp_wfe.o :=-Wa,-march=armv7-a
AFLAGS_ddr3_freq_imx7d.o :=-Wa,-march=armv7-a
AFLAGS_lpddr3_freq_imx.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX7) += pm-imx7.o  ddr3_freq_imx7d.o smp_wfe.o lpddr3_freq_imx.o

obj-$(CONFIG_SOC_IMX50) += mach-imx50.o
obj-$(CONFIG_SOC_IMX51) += mach-imx51.o
obj-$(CONFIG_SOC_IMX53) += mach-imx53.o

obj-$(CONFIG_SOC_VF610) += clk-vf610.o mach-vf610.o

obj-$(CONFIG_SOC_LS1021A) += mach-ls1021a.o

obj-y += devices/
