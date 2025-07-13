// SPDX-License-Identifier: GPL-2.0-only

/* SpacemiT reset controller driver */

#include <linux/auxiliary_bus.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/types.h>

#include <soc/spacemit/k1-syscon.h>
#include <dt-bindings/clock/spacemit,k1-syscon.h>

struct ccu_reset_data {
	u32 offset;
	u32 assert_mask;
	u32 deassert_mask;
};

struct ccu_reset_controller_data {
	const struct ccu_reset_data *reset_data;	/* array */
	size_t count;
};

struct ccu_reset_controller {
	struct reset_controller_dev rcdev;
	const struct ccu_reset_controller_data *data;
	struct regmap *regmap;
};

#define RESET_DATA(_offset, _assert_mask, _deassert_mask)	\
	{							\
		.offset		= (_offset),			\
		.assert_mask	= (_assert_mask),		\
		.deassert_mask	= (_deassert_mask),		\
	}

static const struct ccu_reset_data k1_mpmu_resets[] = {
	[RESET_WDT]	= RESET_DATA(MPMU_WDTPCR,		BIT(2), 0),
};

static const struct ccu_reset_controller_data k1_mpmu_reset_data = {
	.reset_data	= k1_mpmu_resets,
	.count		= ARRAY_SIZE(k1_mpmu_resets),
};

static const struct ccu_reset_data k1_apbc_resets[] = {
	[RESET_UART0]	= RESET_DATA(APBC_UART1_CLK_RST,	BIT(2),	0),
	[RESET_UART2]	= RESET_DATA(APBC_UART2_CLK_RST,	BIT(2), 0),
	[RESET_GPIO]	= RESET_DATA(APBC_GPIO_CLK_RST,		BIT(2), 0),
	[RESET_PWM0]	= RESET_DATA(APBC_PWM0_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM1]	= RESET_DATA(APBC_PWM1_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM2]	= RESET_DATA(APBC_PWM2_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM3]	= RESET_DATA(APBC_PWM3_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM4]	= RESET_DATA(APBC_PWM4_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM5]	= RESET_DATA(APBC_PWM5_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM6]	= RESET_DATA(APBC_PWM6_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM7]	= RESET_DATA(APBC_PWM7_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM8]	= RESET_DATA(APBC_PWM8_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM9]	= RESET_DATA(APBC_PWM9_CLK_RST,		BIT(2), BIT(0)),
	[RESET_PWM10]	= RESET_DATA(APBC_PWM10_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM11]	= RESET_DATA(APBC_PWM11_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM12]	= RESET_DATA(APBC_PWM12_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM13]	= RESET_DATA(APBC_PWM13_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM14]	= RESET_DATA(APBC_PWM14_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM15]	= RESET_DATA(APBC_PWM15_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM16]	= RESET_DATA(APBC_PWM16_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM17]	= RESET_DATA(APBC_PWM17_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM18]	= RESET_DATA(APBC_PWM18_CLK_RST,	BIT(2), BIT(0)),
	[RESET_PWM19]	= RESET_DATA(APBC_PWM19_CLK_RST,	BIT(2), BIT(0)),
	[RESET_SSP3]	= RESET_DATA(APBC_SSP3_CLK_RST,		BIT(2), 0),
	[RESET_UART3]	= RESET_DATA(APBC_UART3_CLK_RST,	BIT(2), 0),
	[RESET_RTC]	= RESET_DATA(APBC_RTC_CLK_RST,		BIT(2), 0),
	[RESET_TWSI0]	= RESET_DATA(APBC_TWSI0_CLK_RST,	BIT(2), 0),
	[RESET_TIMERS1]	= RESET_DATA(APBC_TIMERS1_CLK_RST,	BIT(2), 0),
	[RESET_AIB]	= RESET_DATA(APBC_AIB_CLK_RST,		BIT(2), 0),
	[RESET_TIMERS2]	= RESET_DATA(APBC_TIMERS2_CLK_RST,	BIT(2), 0),
	[RESET_ONEWIRE]	= RESET_DATA(APBC_ONEWIRE_CLK_RST,	BIT(2), 0),
	[RESET_SSPA0]	= RESET_DATA(APBC_SSPA0_CLK_RST,	BIT(2), 0),
	[RESET_SSPA1]	= RESET_DATA(APBC_SSPA1_CLK_RST,	BIT(2), 0),
	[RESET_DRO]	= RESET_DATA(APBC_DRO_CLK_RST,		BIT(2), 0),
	[RESET_IR]	= RESET_DATA(APBC_IR_CLK_RST,		BIT(2), 0),
	[RESET_TWSI1]	= RESET_DATA(APBC_TWSI1_CLK_RST,	BIT(2), 0),
	[RESET_TSEN]	= RESET_DATA(APBC_TSEN_CLK_RST,		BIT(2), 0),
	[RESET_TWSI2]	= RESET_DATA(APBC_TWSI2_CLK_RST,	BIT(2), 0),
	[RESET_TWSI4]	= RESET_DATA(APBC_TWSI4_CLK_RST,	BIT(2), 0),
	[RESET_TWSI5]	= RESET_DATA(APBC_TWSI5_CLK_RST,	BIT(2), 0),
	[RESET_TWSI6]	= RESET_DATA(APBC_TWSI6_CLK_RST,	BIT(2), 0),
	[RESET_TWSI7]	= RESET_DATA(APBC_TWSI7_CLK_RST,	BIT(2), 0),
	[RESET_TWSI8]	= RESET_DATA(APBC_TWSI8_CLK_RST,	BIT(2), 0),
	[RESET_IPC_AP2AUD] = RESET_DATA(APBC_IPC_AP2AUD_CLK_RST, BIT(2), 0),
	[RESET_UART4]	= RESET_DATA(APBC_UART4_CLK_RST,	BIT(2), 0),
	[RESET_UART5]	= RESET_DATA(APBC_UART5_CLK_RST,	BIT(2), 0),
	[RESET_UART6]	= RESET_DATA(APBC_UART6_CLK_RST,	BIT(2), 0),
	[RESET_UART7]	= RESET_DATA(APBC_UART7_CLK_RST,	BIT(2), 0),
	[RESET_UART8]	= RESET_DATA(APBC_UART8_CLK_RST,	BIT(2), 0),
	[RESET_UART9]	= RESET_DATA(APBC_UART9_CLK_RST,	BIT(2), 0),
	[RESET_CAN0]	= RESET_DATA(APBC_CAN0_CLK_RST,		BIT(2), 0),
};

static const struct ccu_reset_controller_data k1_apbc_reset_data = {
	.reset_data	= k1_apbc_resets,
	.count		= ARRAY_SIZE(k1_apbc_resets),
};

static const struct ccu_reset_data k1_apmu_resets[] = {
	[RESET_CCIC_4X]	= RESET_DATA(APMU_CCIC_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_CCIC1_PHY] = RESET_DATA(APMU_CCIC_CLK_RES_CTRL,	0, BIT(2)),
	[RESET_SDH_AXI]	= RESET_DATA(APMU_SDH0_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_SDH0]	= RESET_DATA(APMU_SDH0_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_SDH1]	= RESET_DATA(APMU_SDH1_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_SDH2]	= RESET_DATA(APMU_SDH2_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_USBP1_AXI] = RESET_DATA(APMU_USB_CLK_RES_CTRL,	0, BIT(4)),
	[RESET_USB_AXI]	= RESET_DATA(APMU_USB_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_USB30_AHB] = RESET_DATA(APMU_USB_CLK_RES_CTRL,	0, BIT(9)),
	[RESET_USB30_VCC] = RESET_DATA(APMU_USB_CLK_RES_CTRL,	0, BIT(10)),
	[RESET_USB30_PHY] = RESET_DATA(APMU_USB_CLK_RES_CTRL,	0, BIT(11)),
	[RESET_QSPI]	= RESET_DATA(APMU_QSPI_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_QSPI_BUS] = RESET_DATA(APMU_QSPI_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_DMA]	= RESET_DATA(APMU_DMA_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_AES]	= RESET_DATA(APMU_AES_CLK_RES_CTRL,	0, BIT(4)),
	[RESET_VPU]	= RESET_DATA(APMU_VPU_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_GPU]	= RESET_DATA(APMU_GPU_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_EMMC]	= RESET_DATA(APMU_PMUA_EM_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_EMMC_X]	= RESET_DATA(APMU_PMUA_EM_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_AUDIO_SYS] = RESET_DATA(APMU_AUDIO_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_AUDIO_MCU] = RESET_DATA(APMU_AUDIO_CLK_RES_CTRL, 0, BIT(2)),
	[RESET_AUDIO_APMU] = RESET_DATA(APMU_AUDIO_CLK_RES_CTRL, 0, BIT(3)),
	[RESET_HDMI]	= RESET_DATA(APMU_HDMI_CLK_RES_CTRL,	0, BIT(9)),
	[RESET_PCIE0_DBI] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_0, 0, BIT(3)),
	[RESET_PCIE0_SLAVE] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_0, 0, BIT(4)),
	[RESET_PCIE0_MASTER] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_0, 0, BIT(5)),
	[RESET_PCIE0_GLOBAL] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_0, BIT(8), 0),
	[RESET_PCIE1_DBI] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_1, 0, BIT(3)),
	[RESET_PCIE1_SLAVE] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_1, 0, BIT(4)),
	[RESET_PCIE1_MASTER] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_1, 0, BIT(5)),
	[RESET_PCIE1_GLOBAL] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_1, BIT(8), 0),
	[RESET_PCIE2_DBI] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_2, 0, BIT(3)),
	[RESET_PCIE2_SLAVE] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_2, 0, BIT(4)),
	[RESET_PCIE2_MASTER] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_2, 0, BIT(5)),
	[RESET_PCIE2_GLOBAL] = RESET_DATA(APMU_PCIE_CLK_RES_CTRL_2, BIT(8), 0),
	[RESET_EMAC0]	= RESET_DATA(APMU_EMAC0_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_EMAC1]	= RESET_DATA(APMU_EMAC1_CLK_RES_CTRL,	0, BIT(1)),
	[RESET_JPG]	= RESET_DATA(APMU_JPG_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_CCIC2PHY] = RESET_DATA(APMU_CSI_CCIC2_CLK_RES_CTRL, 0, BIT(2)),
	[RESET_CCIC3PHY] = RESET_DATA(APMU_CSI_CCIC2_CLK_RES_CTRL, 0, BIT(29)),
	[RESET_CSI]	= RESET_DATA(APMU_CSI_CCIC2_CLK_RES_CTRL, 0, BIT(1)),
	[RESET_ISP]	= RESET_DATA(APMU_ISP_CLK_RES_CTRL,	0, BIT(0)),
	[RESET_ISP_CPP]	= RESET_DATA(APMU_ISP_CLK_RES_CTRL,	0, BIT(27)),
	[RESET_ISP_BUS]	= RESET_DATA(APMU_ISP_CLK_RES_CTRL,	0, BIT(3)),
	[RESET_ISP_CI]	= RESET_DATA(APMU_ISP_CLK_RES_CTRL,	0, BIT(16)),
	[RESET_DPU_MCLK] = RESET_DATA(APMU_LCD_CLK_RES_CTRL2,	0, BIT(9)),
	[RESET_DPU_ESC]	= RESET_DATA(APMU_LCD_CLK_RES_CTRL1,	0, BIT(3)),
	[RESET_DPU_HCLK] = RESET_DATA(APMU_LCD_CLK_RES_CTRL1,	0, BIT(4)),
	[RESET_DPU_SPIBUS] = RESET_DATA(APMU_LCD_SPI_CLK_RES_CTRL, 0, BIT(4)),
	[RESET_DPU_SPI_HBUS] = RESET_DATA(APMU_LCD_SPI_CLK_RES_CTRL, 0, BIT(2)),
	[RESET_V2D]	= RESET_DATA(APMU_LCD_CLK_RES_CTRL1,	0, BIT(27)),
	[RESET_MIPI]	= RESET_DATA(APMU_LCD_CLK_RES_CTRL1,	0, BIT(15)),
	[RESET_MC]	= RESET_DATA(APMU_PMUA_MC_CTRL,		0, BIT(0)),
};

static const struct ccu_reset_controller_data k1_apmu_reset_data = {
	.reset_data	= k1_apmu_resets,
	.count		= ARRAY_SIZE(k1_apmu_resets),
};

static const struct ccu_reset_data k1_rcpu_resets[] = {
	[RESET_RCPU_SSP0]	= RESET_DATA(RCPU_SSP0_CLK_RST,	0, BIT(0)),
	[RESET_RCPU_I2C0]	= RESET_DATA(RCPU_I2C0_CLK_RST,	0, BIT(0)),
	[RESET_RCPU_UART1]	= RESET_DATA(RCPU_UART1_CLK_RST, 0, BIT(0)),
	[RESET_RCPU_IR]		= RESET_DATA(RCPU_CAN_CLK_RST,	0, BIT(0)),
	[RESET_RCPU_CAN]	= RESET_DATA(RCPU_IR_CLK_RST,	0, BIT(0)),
	[RESET_RCPU_UART0]	= RESET_DATA(RCPU_UART0_CLK_RST, 0, BIT(0)),
	[RESET_RCPU_HDMI_AUDIO]	= RESET_DATA(AUDIO_HDMI_CLK_CTRL, 0, BIT(0)),
};

static const struct ccu_reset_controller_data k1_rcpu_reset_data = {
	.reset_data	= k1_rcpu_resets,
	.count		= ARRAY_SIZE(k1_rcpu_resets),
};

static const struct ccu_reset_data k1_rcpu2_resets[] = {
	[RESET_RCPU2_PWM0]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM1]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM2]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM3]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM4]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM5]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM6]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM7]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM8]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
	[RESET_RCPU2_PWM9]	= RESET_DATA(RCPU2_PWM9_CLK_RST, BIT(2), BIT(0)),
};

static const struct ccu_reset_controller_data k1_rcpu2_reset_data = {
	.reset_data	= k1_rcpu2_resets,
	.count		= ARRAY_SIZE(k1_rcpu2_resets),
};

static const struct ccu_reset_data k1_apbc2_resets[] = {
	[RESET_APBC2_UART1]	= RESET_DATA(APBC2_UART1_CLK_RST, BIT(2), 0),
	[RESET_APBC2_SSP2]	= RESET_DATA(APBC2_SSP2_CLK_RST, BIT(2), 0),
	[RESET_APBC2_TWSI3]	= RESET_DATA(APBC2_TWSI3_CLK_RST, BIT(2), 0),
	[RESET_APBC2_RTC]	= RESET_DATA(APBC2_RTC_CLK_RST,	BIT(2), 0),
	[RESET_APBC2_TIMERS0]	= RESET_DATA(APBC2_TIMERS0_CLK_RST, BIT(2), 0),
	[RESET_APBC2_KPC]	= RESET_DATA(APBC2_KPC_CLK_RST,	BIT(2), 0),
	[RESET_APBC2_GPIO]	= RESET_DATA(APBC2_GPIO_CLK_RST, BIT(2), 0),
};

static const struct ccu_reset_controller_data k1_apbc2_reset_data = {
	.reset_data	= k1_apbc2_resets,
	.count		= ARRAY_SIZE(k1_apbc2_resets),
};

static int spacemit_reset_update(struct reset_controller_dev *rcdev,
				 unsigned long id, bool assert)
{
	struct ccu_reset_controller *controller;
	const struct ccu_reset_data *data;
	u32 mask;
	u32 val;

	controller = container_of(rcdev, struct ccu_reset_controller, rcdev);
	data = &controller->data->reset_data[id];
	mask = data->assert_mask | data->deassert_mask;
	val = assert ? data->assert_mask : data->deassert_mask;

	return regmap_update_bits(controller->regmap, data->offset, mask, val);
}

static int spacemit_reset_assert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return spacemit_reset_update(rcdev, id, true);
}

static int spacemit_reset_deassert(struct reset_controller_dev *rcdev,
				   unsigned long id)
{
	return spacemit_reset_update(rcdev, id, false);
}

static const struct reset_control_ops spacemit_reset_control_ops = {
	.assert		= spacemit_reset_assert,
	.deassert	= spacemit_reset_deassert,
};

static int spacemit_reset_controller_register(struct device *dev,
					      struct ccu_reset_controller *controller)
{
	struct reset_controller_dev *rcdev = &controller->rcdev;

	rcdev->ops = &spacemit_reset_control_ops;
	rcdev->owner = THIS_MODULE;
	rcdev->of_node = dev->of_node;
	rcdev->nr_resets = controller->data->count;

	return devm_reset_controller_register(dev, &controller->rcdev);
}

static int spacemit_reset_probe(struct auxiliary_device *adev,
				const struct auxiliary_device_id *id)
{
	struct spacemit_ccu_adev *rdev = to_spacemit_ccu_adev(adev);
	struct ccu_reset_controller *controller;
	struct device *dev = &adev->dev;

	controller = devm_kzalloc(dev, sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return -ENOMEM;
	controller->data = (const struct ccu_reset_controller_data *)id->driver_data;
	controller->regmap = rdev->regmap;

	return spacemit_reset_controller_register(dev, controller);
}

#define K1_AUX_DEV_ID(_unit) \
	{ \
		.name = "spacemit_ccu_k1." #_unit "-reset", \
		.driver_data = (kernel_ulong_t)&k1_ ## _unit ## _reset_data, \
	}

static const struct auxiliary_device_id spacemit_reset_ids[] = {
	K1_AUX_DEV_ID(mpmu),
	K1_AUX_DEV_ID(apbc),
	K1_AUX_DEV_ID(apmu),
	K1_AUX_DEV_ID(rcpu),
	K1_AUX_DEV_ID(rcpu2),
	K1_AUX_DEV_ID(apbc2),
	{ },
};
MODULE_DEVICE_TABLE(auxiliary, spacemit_reset_ids);

static struct auxiliary_driver spacemit_k1_reset_driver = {
	.probe          = spacemit_reset_probe,
	.id_table       = spacemit_reset_ids,
};
module_auxiliary_driver(spacemit_k1_reset_driver);

MODULE_AUTHOR("Alex Elder <elder@kernel.org>");
MODULE_DESCRIPTION("SpacemiT reset controller driver");
MODULE_LICENSE("GPL");
