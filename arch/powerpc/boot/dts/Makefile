# SPDX-License-Identifier: GPL-2.0

subdir-y += fsl

dtb-$(CONFIG_OF_ALL_DTBS) := $(patsubst $(src)/%.dts,%.dtb, $(wildcard $(src)/*.dts))

# PPC44x platforms
dtb-$(CONFIG_PPC44x_SIMPLE) += arches.dtb bamboo.dtb bluestone.dtb glacier.dtb
dtb-$(CONFIG_PPC44x_SIMPLE) += eiger.dtb katmai.dtb rainier.dtb redwood.dtb
dtb-$(CONFIG_PPC44x_SIMPLE) += sequoia.dtb taishan.dtb yosemite.dtb icon.dtb
dtb-$(CONFIG_EBONY) += ebony.dtb
dtb-$(CONFIG_SAM440EP) += sam440ep.dtb
dtb-$(CONFIG_WARP) += warp.dtb
dtb-$(CONFIG_ISS4xx) += iss4xx.dtb iss4xx-mpic.dtb
dtb-$(CONFIG_CANYONLANDS) += canyonlands.dtb
dtb-$(CONFIG_CURRITUCK) += currituck.dtb
dtb-$(CONFIG_AKEBONO) += akebono.dtb
dtb-$(CONFIG_FSP2) += fsp2.dtb

# Embedded 6xx platforms
dtb-$(CONFIG_LINKSTATION) += kuroboxHG.dtb kuroboxHD.dtb
dtb-$(CONFIG_STORCENTER) += storcenter.dtb
dtb-$(CONFIG_PPC_HOLLY) += holly.dtb
dtb-$(CONFIG_GAMECUBE) += gamecube.dtb
dtb-$(CONFIG_WII) += wii.dtb
dtb-$(CONFIG_MVME5100) += mvme5100.dtb

# MPC8xx platforms
dtb-$(CONFIG_MPC885ADS) += mpc885ads.dtb
dtb-$(CONFIG_MPC86XADS) += mpc866ads.dtb
dtb-$(CONFIG_PPC_EP88XC) += ep88xc.dtb
dtb-$(CONFIG_PPC_ADDER875) += adder875-redboot.dtb adder875-uboot.dtb
dtb-$(CONFIG_TQM8XX) += tqm8xx.dtb

# MPC512x platforms
dtb-$(CONFIG_MPC5121_ADS) += mpc5121ads.dtb
dtb-$(CONFIG_MPC512x_GENERIC) += mpc5125twr.dtb ac14xx.dts
dtb-$(CONFIG_PDM360NG) += pdm360ng.dtb

# MPC5200 platforms
dtb-$(CONFIG_PPC_MPC5200_SIMPLE) += a3m071.dtb a4m072.dtb charon.dtb cm5200.dtb
dtb-$(CONFIG_PPC_MPC5200_SIMPLE) += digsy_mtc.dtb motionpro.dtb mucmc52.dtb
dtb-$(CONFIG_PPC_MPC5200_SIMPLE) += o2d.dtb o2d300.dtb o2dnt2.dtb o2i.dtb
dtb-$(CONFIG_PPC_MPC5200_SIMPLE) += o2mnt.dtb o3dnt.dtb pcm030.dtb pcm032.dtb
dtb-$(CONFIG_PPC_MPC5200_SIMPLE) += tqm5200.dtb uc101.dtb
dtb-$(CONFIG_PPC_LITE5200) += lite5200.dtb lite5200b.dtb
dtb-$(CONFIG_PPC_MEDIA5200) += media5200.dtb

# MPC82xx platforms
dtb-$(CONFIG_EP8248E) += ep8248e.dtb
dtb-$(CONFIG_MGCOGE) += mgcoge.dtb

# MPC83xx platforms
dtb-$(CONFIG_MPC830x_RDB) += mpc8308rdb.dtb mpc8308_p1m.dtb
dtb-$(CONFIG_MPC831x_RDB) += mpc8313erdb.dtb mpc8315erdb.dtb
dtb-$(CONFIG_MPC832x_RDB) += mpc832x_rdb.dtb
dtb-$(CONFIG_MPC834x_ITX) += mpc8349emitx.dtb mpc8349emitxgp.dtb
dtb-$(CONFIG_ASP834x) += asp834x-redboot.dtb
dtb-$(CONFIG_MPC836x_RDK) += mpc836x_rdk.dtb
dtb-$(CONFIG_KMETER1) += kmeter1.dtb
dtb-$(CONFIG_MPC837x_RDB) += mpc8377_rdb.dtb mpc8378_rdb.dtb mpc8379_rdb.dtb
dtb-$(CONFIG_MPC837x_RDB) += mpc8377_wlan.dtb

# MPC85xx platforms
dtb-$(CONFIG_STX_GP3) += stx_gp3_8560.dtb stxssa8555.dtb
dtb-$(CONFIG_TQM85xx) += tqm8540.dtb tqm8541.dtb tqm8548.dtb
dtb-$(CONFIG_TQM85xx) += tqm8548-bigflash.dtb tqm8555.dtb tqm8560.dtb
dtb-$(CONFIG_SOCRATES) += socrates.dtb
dtb-$(CONFIG_KSI8560) += ksi8560.dtb
dtb-$(CONFIG_XES_MPC85xx) += xcalibur1501.dtb xpedite5200.dtb
dtb-$(CONFIG_XES_MPC85xx) += xpedite5200_xmon.dtb xpedite5301.dtb
dtb-$(CONFIG_XES_MPC85xx) += xpedite5330.dtb xpedite5370.dtb
dtb-$(CONFIG_PPC_P2020) += turris1x.dtb

# Misc. platforms
dtb-$(CONFIG_PPC_MICROWATT) += microwatt.dtb
dtb-$(CONFIG_AMIGAONE) += amigaone.dtb
dtb-$(CONFIG_PPC_PS3) += ps3.dtb
