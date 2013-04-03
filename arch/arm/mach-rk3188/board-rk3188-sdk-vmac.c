#define grf_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK30_GRF_BASE + offset); dsb(); } while (0)

static int rk30_vmac_register_set(void)
{
	//config rk30 vmac as rmii
	writel_relaxed(0x3 << 16 | 0x2, RK30_GRF_BASE + GRF_SOC_CON1);
	return 0;
}

static int rk30_rmii_io_init(void)
{
	int err;
	printk("enter %s ",__func__);
	iomux_set(GPIO0_C0);//power pwr
	iomux_set(GPIO3_D2);//int
	
	iomux_set(RMII_MD);//IO3_D0
	iomux_set(RMII_MDCLK);//IO3_D1
      
	iomux_set(RMII_RXD0);
	iomux_set(RMII_RXD1);
	iomux_set(RMII_CRS);
	iomux_set(RMII_RXERR);
	iomux_set(RMII_TXD0);
	iomux_set(RMII_TXD1);
	iomux_set(RMII_TXEN);
	iomux_set(RMII_CLKOUT);

	//rk3188 gpio3 and sdio drive strength , 
      grf_writel(0x0f<16|0x0f,GRF_IO_CON3);
      
	//phy power gpio
	err = gpio_request(PHY_PWR_EN_GPIO, "phy_power_en");
	if (err) {
	      printk("request phy power en pin faile ! \n");
		return -1;
	}
	//phy power down
	gpio_direction_output(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
	gpio_set_value(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);

	return 0;
}

static int rk30_rmii_io_deinit(void)
{
	//phy power down
	printk("enter %s ",__func__);
	gpio_direction_output(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
	gpio_set_value(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
	//free
	gpio_free(PHY_PWR_EN_GPIO);
	return 0;
}

static int rk30_rmii_power_control(int enable)
{
      printk("enter %s ,enable = %d ",__func__,enable);
	if (enable) {
		//enable phy power
		printk("power on phy\n");
		iomux_set(GPIO0_C0);//power pwr
        	iomux_set(GPIO3_D2);//int
        	
        	iomux_set(RMII_MD);//IO3_D0
        	iomux_set(RMII_MDCLK);//IO3_D1
              
        	iomux_set(RMII_RXD0);
        	iomux_set(RMII_RXD1);
        	iomux_set(RMII_CRS);
        	iomux_set(RMII_RXERR);
        	iomux_set(RMII_TXD0);
        	iomux_set(RMII_TXD1);
        	iomux_set(RMII_TXEN);
        	iomux_set(RMII_CLKOUT);
	
		gpio_direction_output(PHY_PWR_EN_GPIO, PHY_PWR_EN_VALUE);
		gpio_set_value(PHY_PWR_EN_GPIO, PHY_PWR_EN_VALUE);

		//gpio reset		
	}else {
		gpio_direction_output(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
		gpio_set_value(PHY_PWR_EN_GPIO, !PHY_PWR_EN_VALUE);
	}
	return 0;
}

#define BIT_EMAC_SPEED      (1 << 1)
static int rk29_vmac_speed_switch(int speed)
{
	if (10 == speed) {
	    writel_relaxed(readl_relaxed(RK30_GRF_BASE + GRF_SOC_CON1) & (~BIT_EMAC_SPEED), RK30_GRF_BASE + GRF_SOC_CON1);
	} else {
	    writel_relaxed(readl_relaxed(RK30_GRF_BASE + GRF_SOC_CON1) | ( BIT_EMAC_SPEED), RK30_GRF_BASE + GRF_SOC_CON1);
	}
}

struct rk29_vmac_platform_data board_vmac_data = {
	.vmac_register_set = rk30_vmac_register_set,
	.rmii_io_init = rk30_rmii_io_init,
	.rmii_io_deinit = rk30_rmii_io_deinit,
	.rmii_power_control = rk30_rmii_power_control,
	.rmii_speed_switch = rk29_vmac_speed_switch,
};
