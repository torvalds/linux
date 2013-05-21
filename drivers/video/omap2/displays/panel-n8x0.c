/* #define DEBUG */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/fb.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#define BLIZZARD_REV_CODE                      0x00
#define BLIZZARD_CONFIG                        0x02
#define BLIZZARD_PLL_DIV                       0x04
#define BLIZZARD_PLL_LOCK_RANGE                0x06
#define BLIZZARD_PLL_CLOCK_SYNTH_0             0x08
#define BLIZZARD_PLL_CLOCK_SYNTH_1             0x0a
#define BLIZZARD_PLL_MODE                      0x0c
#define BLIZZARD_CLK_SRC                       0x0e
#define BLIZZARD_MEM_BANK0_ACTIVATE            0x10
#define BLIZZARD_MEM_BANK0_STATUS              0x14
#define BLIZZARD_PANEL_CONFIGURATION           0x28
#define BLIZZARD_HDISP                         0x2a
#define BLIZZARD_HNDP                          0x2c
#define BLIZZARD_VDISP0                        0x2e
#define BLIZZARD_VDISP1                        0x30
#define BLIZZARD_VNDP                          0x32
#define BLIZZARD_HSW                           0x34
#define BLIZZARD_VSW                           0x38
#define BLIZZARD_DISPLAY_MODE                  0x68
#define BLIZZARD_INPUT_WIN_X_START_0           0x6c
#define BLIZZARD_DATA_SOURCE_SELECT            0x8e
#define BLIZZARD_DISP_MEM_DATA_PORT            0x90
#define BLIZZARD_DISP_MEM_READ_ADDR0           0x92
#define BLIZZARD_POWER_SAVE                    0xE6
#define BLIZZARD_NDISP_CTRL_STATUS             0xE8

/* Data source select */
/* For S1D13745 */
#define BLIZZARD_SRC_WRITE_LCD_BACKGROUND	0x00
#define BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE	0x01
#define BLIZZARD_SRC_WRITE_OVERLAY_ENABLE	0x04
#define BLIZZARD_SRC_DISABLE_OVERLAY		0x05
/* For S1D13744 */
#define BLIZZARD_SRC_WRITE_LCD			0x00
#define BLIZZARD_SRC_BLT_LCD			0x06

#define BLIZZARD_COLOR_RGB565			0x01
#define BLIZZARD_COLOR_YUV420			0x09

#define BLIZZARD_VERSION_S1D13745		0x01	/* Hailstorm */
#define BLIZZARD_VERSION_S1D13744		0x02	/* Blizzard */

#define MIPID_CMD_READ_DISP_ID		0x04
#define MIPID_CMD_READ_RED		0x06
#define MIPID_CMD_READ_GREEN		0x07
#define MIPID_CMD_READ_BLUE		0x08
#define MIPID_CMD_READ_DISP_STATUS	0x09
#define MIPID_CMD_RDDSDR		0x0F
#define MIPID_CMD_SLEEP_IN		0x10
#define MIPID_CMD_SLEEP_OUT		0x11
#define MIPID_CMD_DISP_OFF		0x28
#define MIPID_CMD_DISP_ON		0x29

static struct panel_drv_data {
	struct mutex lock;

	struct omap_dss_device *dssdev;
	struct spi_device *spidev;

	int blizzard_ver;
} s_drv_data;


static inline
struct panel_n8x0_data *get_board_data(const struct omap_dss_device *dssdev)
{
	return dssdev->data;
}

static inline
struct panel_drv_data *get_drv_data(const struct omap_dss_device *dssdev)
{
	return &s_drv_data;
}


static inline void blizzard_cmd(u8 cmd)
{
	omap_rfbi_write_command(&cmd, 1);
}

static inline void blizzard_write(u8 cmd, const u8 *buf, int len)
{
	omap_rfbi_write_command(&cmd, 1);
	omap_rfbi_write_data(buf, len);
}

static inline void blizzard_read(u8 cmd, u8 *buf, int len)
{
	omap_rfbi_write_command(&cmd, 1);
	omap_rfbi_read_data(buf, len);
}

static u8 blizzard_read_reg(u8 cmd)
{
	u8 data;
	blizzard_read(cmd, &data, 1);
	return data;
}

static void blizzard_ctrl_setup_update(struct omap_dss_device *dssdev,
		int x, int y, int w, int h)
{
	struct panel_drv_data *ddata = get_drv_data(dssdev);
	u8 tmp[18];
	int x_end, y_end;

	x_end = x + w - 1;
	y_end = y + h - 1;

	tmp[0] = x;
	tmp[1] = x >> 8;
	tmp[2] = y;
	tmp[3] = y >> 8;
	tmp[4] = x_end;
	tmp[5] = x_end >> 8;
	tmp[6] = y_end;
	tmp[7] = y_end >> 8;

	/* scaling? */
	tmp[8] = x;
	tmp[9] = x >> 8;
	tmp[10] = y;
	tmp[11] = y >> 8;
	tmp[12] = x_end;
	tmp[13] = x_end >> 8;
	tmp[14] = y_end;
	tmp[15] = y_end >> 8;

	tmp[16] = BLIZZARD_COLOR_RGB565;

	if (ddata->blizzard_ver == BLIZZARD_VERSION_S1D13745)
		tmp[17] = BLIZZARD_SRC_WRITE_LCD_BACKGROUND;
	else
		tmp[17] = ddata->blizzard_ver == BLIZZARD_VERSION_S1D13744 ?
			BLIZZARD_SRC_WRITE_LCD :
			BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE;

	omapdss_rfbi_set_pixel_size(dssdev, 16);
	omapdss_rfbi_set_data_lines(dssdev, 8);

	omap_rfbi_configure(dssdev);

	blizzard_write(BLIZZARD_INPUT_WIN_X_START_0, tmp, 18);

	omapdss_rfbi_set_pixel_size(dssdev, 16);
	omapdss_rfbi_set_data_lines(dssdev, 16);

	omap_rfbi_configure(dssdev);
}

static void mipid_transfer(struct spi_device *spi, int cmd, const u8 *wbuf,
		int wlen, u8 *rbuf, int rlen)
{
	struct spi_message	m;
	struct spi_transfer	*x, xfer[4];
	u16			w;
	int			r;

	spi_message_init(&m);

	memset(xfer, 0, sizeof(xfer));
	x = &xfer[0];

	cmd &=  0xff;
	x->tx_buf		= &cmd;
	x->bits_per_word	= 9;
	x->len			= 2;
	spi_message_add_tail(x, &m);

	if (wlen) {
		x++;
		x->tx_buf		= wbuf;
		x->len			= wlen;
		x->bits_per_word	= 9;
		spi_message_add_tail(x, &m);
	}

	if (rlen) {
		x++;
		x->rx_buf	= &w;
		x->len		= 1;
		spi_message_add_tail(x, &m);

		if (rlen > 1) {
			/* Arrange for the extra clock before the first
			 * data bit.
			 */
			x->bits_per_word = 9;
			x->len		 = 2;

			x++;
			x->rx_buf	 = &rbuf[1];
			x->len		 = rlen - 1;
			spi_message_add_tail(x, &m);
		}
	}

	r = spi_sync(spi, &m);
	if (r < 0)
		dev_dbg(&spi->dev, "spi_sync %d\n", r);

	if (rlen)
		rbuf[0] = w & 0xff;
}

static inline void mipid_cmd(struct spi_device *spi, int cmd)
{
	mipid_transfer(spi, cmd, NULL, 0, NULL, 0);
}

static inline void mipid_write(struct spi_device *spi,
		int reg, const u8 *buf, int len)
{
	mipid_transfer(spi, reg, buf, len, NULL, 0);
}

static inline void mipid_read(struct spi_device *spi,
		int reg, u8 *buf, int len)
{
	mipid_transfer(spi, reg, NULL, 0, buf, len);
}

static void set_data_lines(struct spi_device *spi, int data_lines)
{
	u16 par;

	switch (data_lines) {
	case 16:
		par = 0x150;
		break;
	case 18:
		par = 0x160;
		break;
	case 24:
		par = 0x170;
		break;
	}

	mipid_write(spi, 0x3a, (u8 *)&par, 2);
}

static void send_init_string(struct spi_device *spi)
{
	u16 initpar[] = { 0x0102, 0x0100, 0x0100 };
	mipid_write(spi, 0xc2, (u8 *)initpar, sizeof(initpar));
}

static void send_display_on(struct spi_device *spi)
{
	mipid_cmd(spi, MIPID_CMD_DISP_ON);
}

static void send_display_off(struct spi_device *spi)
{
	mipid_cmd(spi, MIPID_CMD_DISP_OFF);
}

static void send_sleep_out(struct spi_device *spi)
{
	mipid_cmd(spi, MIPID_CMD_SLEEP_OUT);
	msleep(120);
}

static void send_sleep_in(struct spi_device *spi)
{
	mipid_cmd(spi, MIPID_CMD_SLEEP_IN);
	msleep(50);
}

static int n8x0_panel_power_on(struct omap_dss_device *dssdev)
{
	int r;
	struct panel_n8x0_data *bdata = get_board_data(dssdev);
	struct panel_drv_data *ddata = get_drv_data(dssdev);
	struct spi_device *spi = ddata->spidev;
	u8 rev, conf;
	u8 display_id[3];
	const char *panel_name;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	gpio_direction_output(bdata->ctrl_pwrdown, 1);

	omapdss_rfbi_set_size(dssdev, dssdev->panel.timings.x_res,
		dssdev->panel.timings.y_res);
	omapdss_rfbi_set_pixel_size(dssdev, dssdev->ctrl.pixel_size);
	omapdss_rfbi_set_data_lines(dssdev, dssdev->phy.rfbi.data_lines);
	omapdss_rfbi_set_interface_timings(dssdev, &dssdev->ctrl.rfbi_timings);

	r = omapdss_rfbi_display_enable(dssdev);
	if (r)
		goto err_rfbi_en;

	rev = blizzard_read_reg(BLIZZARD_REV_CODE);
	conf = blizzard_read_reg(BLIZZARD_CONFIG);

	switch (rev & 0xfc) {
	case 0x9c:
		ddata->blizzard_ver = BLIZZARD_VERSION_S1D13744;
		dev_info(&dssdev->dev, "s1d13744 LCD controller rev %d "
			"initialized (CNF pins %x)\n", rev & 0x03, conf & 0x07);
		break;
	case 0xa4:
		ddata->blizzard_ver = BLIZZARD_VERSION_S1D13745;
		dev_info(&dssdev->dev, "s1d13745 LCD controller rev %d "
			"initialized (CNF pins %x)\n", rev & 0x03, conf & 0x07);
		break;
	default:
		dev_err(&dssdev->dev, "invalid s1d1374x revision %02x\n", rev);
		r = -ENODEV;
		goto err_inv_chip;
	}

	/* panel */

	gpio_direction_output(bdata->panel_reset, 1);

	mipid_read(spi, MIPID_CMD_READ_DISP_ID, display_id, 3);
	dev_dbg(&spi->dev, "MIPI display ID: %02x%02x%02x\n",
			display_id[0], display_id[1], display_id[2]);

	switch (display_id[0]) {
	case 0x45:
		panel_name = "lph8923";
		break;
	case 0x83:
		panel_name = "ls041y3";
		break;
	default:
		dev_err(&dssdev->dev, "invalid display ID 0x%x\n",
				display_id[0]);
		r = -ENODEV;
		goto err_inv_panel;
	}

	dev_info(&dssdev->dev, "%s rev %02x LCD detected\n",
			panel_name, display_id[1]);

	send_sleep_out(spi);
	send_init_string(spi);
	set_data_lines(spi, 24);
	send_display_on(spi);

	return 0;

err_inv_panel:
	/*
	 * HACK: we should turn off the panel here, but there is some problem
	 * with the initialization sequence, and we fail to init the panel if we
	 * have turned it off
	 */
	/* gpio_direction_output(bdata->panel_reset, 0); */
err_inv_chip:
	omapdss_rfbi_display_disable(dssdev);
err_rfbi_en:
	gpio_direction_output(bdata->ctrl_pwrdown, 0);
	return r;
}

static void n8x0_panel_power_off(struct omap_dss_device *dssdev)
{
	struct panel_n8x0_data *bdata = get_board_data(dssdev);
	struct panel_drv_data *ddata = get_drv_data(dssdev);
	struct spi_device *spi = ddata->spidev;

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	send_display_off(spi);
	send_sleep_in(spi);

	/*
	 * HACK: we should turn off the panel here, but there is some problem
	 * with the initialization sequence, and we fail to init the panel if we
	 * have turned it off
	 */
	/* gpio_direction_output(bdata->panel_reset, 0); */
	gpio_direction_output(bdata->ctrl_pwrdown, 0);
	omapdss_rfbi_display_disable(dssdev);
}

static const struct rfbi_timings n8x0_panel_timings = {
	.cs_on_time     = 0,

	.we_on_time     = 9000,
	.we_off_time    = 18000,
	.we_cycle_time  = 36000,

	.re_on_time     = 9000,
	.re_off_time    = 27000,
	.re_cycle_time  = 36000,

	.access_time    = 27000,
	.cs_off_time    = 36000,

	.cs_pulse_width = 0,
};

static int n8x0_panel_probe(struct omap_dss_device *dssdev)
{
	struct panel_n8x0_data *bdata = get_board_data(dssdev);
	struct panel_drv_data *ddata;
	int r;

	dev_dbg(&dssdev->dev, "probe\n");

	if (!bdata)
		return -EINVAL;

	s_drv_data.dssdev = dssdev;

	ddata = &s_drv_data;

	mutex_init(&ddata->lock);

	dssdev->panel.timings.x_res = 800;
	dssdev->panel.timings.y_res = 480;
	dssdev->ctrl.pixel_size = 16;
	dssdev->ctrl.rfbi_timings = n8x0_panel_timings;
	dssdev->caps = OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE;

	if (gpio_is_valid(bdata->panel_reset)) {
		r = devm_gpio_request_one(&dssdev->dev, bdata->panel_reset,
				GPIOF_OUT_INIT_LOW, "PANEL RESET");
		if (r)
			return r;
	}

	if (gpio_is_valid(bdata->ctrl_pwrdown)) {
		r = devm_gpio_request_one(&dssdev->dev, bdata->ctrl_pwrdown,
				GPIOF_OUT_INIT_LOW, "PANEL PWRDOWN");
		if (r)
			return r;
	}

	return 0;
}

static void n8x0_panel_remove(struct omap_dss_device *dssdev)
{
	dev_dbg(&dssdev->dev, "remove\n");

	dev_set_drvdata(&dssdev->dev, NULL);
}

static int n8x0_panel_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = get_drv_data(dssdev);
	int r;

	dev_dbg(&dssdev->dev, "enable\n");

	mutex_lock(&ddata->lock);

	rfbi_bus_lock();

	r = n8x0_panel_power_on(dssdev);

	rfbi_bus_unlock();

	if (r) {
		mutex_unlock(&ddata->lock);
		return r;
	}

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	mutex_unlock(&ddata->lock);

	return 0;
}

static void n8x0_panel_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = get_drv_data(dssdev);

	dev_dbg(&dssdev->dev, "disable\n");

	mutex_lock(&ddata->lock);

	rfbi_bus_lock();

	n8x0_panel_power_off(dssdev);

	rfbi_bus_unlock();

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	mutex_unlock(&ddata->lock);
}

static void n8x0_panel_get_resolution(struct omap_dss_device *dssdev,
		u16 *xres, u16 *yres)
{
	*xres = dssdev->panel.timings.x_res;
	*yres = dssdev->panel.timings.y_res;
}

static void update_done(void *data)
{
	rfbi_bus_unlock();
}

static int n8x0_panel_update(struct omap_dss_device *dssdev,
		u16 x, u16 y, u16 w, u16 h)
{
	struct panel_drv_data *ddata = get_drv_data(dssdev);
	u16 dw, dh;

	dev_dbg(&dssdev->dev, "update\n");

	dw = dssdev->panel.timings.x_res;
	dh = dssdev->panel.timings.y_res;

	if (x != 0 || y != 0 || w != dw || h != dh) {
		dev_err(&dssdev->dev, "invalid update region %d, %d, %d, %d\n",
			x, y, w, h);
		return -EINVAL;
	}

	mutex_lock(&ddata->lock);
	rfbi_bus_lock();

	blizzard_ctrl_setup_update(dssdev, x, y, w, h);

	omap_rfbi_update(dssdev, update_done, NULL);

	mutex_unlock(&ddata->lock);

	return 0;
}

static int n8x0_panel_sync(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = get_drv_data(dssdev);

	dev_dbg(&dssdev->dev, "sync\n");

	mutex_lock(&ddata->lock);
	rfbi_bus_lock();
	rfbi_bus_unlock();
	mutex_unlock(&ddata->lock);

	return 0;
}

static struct omap_dss_driver n8x0_panel_driver = {
	.probe		= n8x0_panel_probe,
	.remove		= n8x0_panel_remove,

	.enable		= n8x0_panel_enable,
	.disable	= n8x0_panel_disable,

	.update		= n8x0_panel_update,
	.sync		= n8x0_panel_sync,

	.get_resolution	= n8x0_panel_get_resolution,
	.get_recommended_bpp = omapdss_default_get_recommended_bpp,

	.driver         = {
		.name   = "n8x0_panel",
		.owner  = THIS_MODULE,
	},
};

/* PANEL */

static int mipid_spi_probe(struct spi_device *spi)
{
	int r;

	dev_dbg(&spi->dev, "mipid_spi_probe\n");

	spi->mode = SPI_MODE_0;

	s_drv_data.spidev = spi;

	r = omap_dss_register_driver(&n8x0_panel_driver);
	if (r)
		pr_err("n8x0_panel: dss driver registration failed\n");

	return r;
}

static int mipid_spi_remove(struct spi_device *spi)
{
	dev_dbg(&spi->dev, "mipid_spi_remove\n");
	omap_dss_unregister_driver(&n8x0_panel_driver);
	return 0;
}

static struct spi_driver mipid_spi_driver = {
	.driver = {
		.name	= "lcd_mipid",
		.owner	= THIS_MODULE,
	},
	.probe	= mipid_spi_probe,
	.remove	= mipid_spi_remove,
};
module_spi_driver(mipid_spi_driver);

MODULE_LICENSE("GPL");
