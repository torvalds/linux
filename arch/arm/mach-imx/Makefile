obj-y := time.o cpu.o system.o irq-common.o

obj-$(CONFIG_SOC_IMX1) += clk-imx1.o mm-imx1.o
obj-$(CONFIG_SOC_IMX21) += clk-imx21.o mm-imx21.o

obj-$(CONFIG_SOC_IMX25) += clk-imx25.o mm-imx25.o ehci-imx25.o cpu-imx25.o

obj-$(CONFIG_SOC_IMX27) += cpu-imx27.o pm-imx27.o
obj-$(CONFIG_SOC_IMX27) += clk-imx27.o mm-imx27.o ehci-imx27.o

obj-$(CONFIG_SOC_IMX31) += mm-imx3.o cpu-imx31.o clk-imx31.o iomux-imx31.o ehci-imx31.o pm-imx3.o
obj-$(CONFIG_SOC_IMX35) += mm-imx3.o cpu-imx35.o clk-imx35.o ehci-imx35.o pm-imx3.o

imx5-pm-$(CONFIG_PM) += pm-imx5.o
obj-$(CONFIG_SOC_IMX5) += cpu-imx5.o mm-imx5.o clk-imx51-imx53.o ehci-imx5.o $(imx5-pm-y) cpu_op-mx51.o

obj-$(CONFIG_COMMON_CLK) += clk-pllv1.o clk-pllv2.o clk-pllv3.o clk-gate2.o \
			    clk-pfd.o clk-busy.o clk.o

obj-$(CONFIG_IMX_HAVE_IOMUX_V1) += iomux-v1.o
obj-$(CONFIG_ARCH_MXC_IOMUX_V3) += iomux-v3.o

obj-$(CONFIG_MXC_TZIC) += tzic.o
obj-$(CONFIG_MXC_AVIC) += avic.o

obj-$(CONFIG_IRAM_ALLOC) += iram_alloc.o
obj-$(CONFIG_MXC_ULPI) += ulpi.o
obj-$(CONFIG_MXC_USE_EPIT) += epit.o
obj-$(CONFIG_MXC_DEBUG_BOARD) += 3ds_debugboard.o
obj-$(CONFIG_CPU_FREQ_IMX)    += cpufreq.o
obj-$(CONFIG_CPU_IDLE) += cpuidle.o

ifdef CONFIG_SND_IMX_SOC
obj-y += ssi-fiq.o
obj-y += ssi-fiq-ksym.o
endif

# Support for CMOS sensor interface
obj-$(CONFIG_MX1_VIDEO) += mx1-camera-fiq.o mx1-camera-fiq-ksym.o

# i.MX1 based machines
obj-$(CONFIG_ARCH_MX1ADS) += mach-mx1ads.o
obj-$(CONFIG_MACH_SCB9328) += mach-scb9328.o
obj-$(CONFIG_MACH_APF9328) += mach-apf9328.o

# i.MX21 based machines
obj-$(CONFIG_MACH_MX21ADS) += mach-mx21ads.o

# i.MX25 based machines
obj-$(CONFIG_MACH_MX25_3DS) += mach-mx25_3ds.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX25SD) += mach-eukrea_cpuimx25.o
obj-$(CONFIG_MACH_EUKREA_MBIMXSD25_BASEBOARD) += eukrea_mbimxsd25-baseboard.o
obj-$(CONFIG_MACH_IMX25_DT) += imx25-dt.o

# i.MX27 based machines
obj-$(CONFIG_MACH_MX27ADS) += mach-mx27ads.o
obj-$(CONFIG_MACH_PCM038) += mach-pcm038.o
obj-$(CONFIG_MACH_PCM970_BASEBOARD) += pcm970-baseboard.o
obj-$(CONFIG_MACH_MX27_3DS) += mach-mx27_3ds.o
obj-$(CONFIG_MACH_IMX27LITE) += mach-imx27lite.o
obj-$(CONFIG_MACH_IMX27_VISSTRIM_M10) += mach-imx27_visstrim_m10.o
obj-$(CONFIG_MACH_CPUIMX27) += mach-cpuimx27.o
obj-$(CONFIG_MACH_EUKREA_MBIMX27_BASEBOARD) += eukrea_mbimx27-baseboard.o
obj-$(CONFIG_MACH_PCA100) += mach-pca100.o
obj-$(CONFIG_MACH_MXT_TD60) += mach-mxt_td60.o
obj-$(CONFIG_MACH_IMX27IPCAM) += mach-imx27ipcam.o
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

obj-$(CONFIG_HAVE_IMX_GPC) += gpc.o
obj-$(CONFIG_HAVE_IMX_MMDC) += mmdc.o
obj-$(CONFIG_HAVE_IMX_SRC) += src.o
AFLAGS_headsmp.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SMP) += headsmp.o platsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += hotplug.o
obj-$(CONFIG_SOC_IMX6Q) += clk-imx6q.o mach-imx6q.o

ifeq ($(CONFIG_PM),y)
obj-$(CONFIG_SOC_IMX6Q) += pm-imx6q.o headsmp.o
endif

# i.MX5 based machines
obj-$(CONFIG_MACH_MX51_BABBAGE) += mach-mx51_babbage.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX51SD) += mach-cpuimx51sd.o
obj-$(CONFIG_MACH_EUKREA_MBIMXSD51_BASEBOARD) += eukrea_mbimxsd51-baseboard.o

obj-$(CONFIG_MACH_IMX51_DT) += imx51-dt.o
obj-$(CONFIG_SOC_IMX53) += mach-imx53.o

obj-y += devices/
