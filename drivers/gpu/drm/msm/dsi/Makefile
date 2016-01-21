ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/msm -Idrivers/gpu/drm/msm/dsi

obj-y := dsi.o dsi_cfg.o dsi_host.o dsi_manager.o phy/dsi_phy.o

obj-$(CONFIG_DRM_MSM_DSI_28NM_PHY) += phy/dsi_phy_28nm.o
obj-$(CONFIG_DRM_MSM_DSI_20NM_PHY) += phy/dsi_phy_20nm.o
obj-$(CONFIG_DRM_MSM_DSI_28NM_8960_PHY) += phy/dsi_phy_28nm_8960.o

ifeq ($(CONFIG_DRM_MSM_DSI_PLL),y)
obj-y += pll/dsi_pll.o
obj-$(CONFIG_DRM_MSM_DSI_28NM_PHY) += pll/dsi_pll_28nm.o
obj-$(CONFIG_DRM_MSM_DSI_28NM_8960_PHY) += pll/dsi_pll_28nm_8960.o
endif
