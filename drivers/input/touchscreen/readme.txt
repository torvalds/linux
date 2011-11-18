touch screen driver 现状描述：
@2011.11.16
	ctp_platform_ops.h: 建立3.0 初始版本；
	1. 新增i2c 相关的detect接口；
	2. 更新set_gpio_irq_mode接口，能实现外部32个中断源的配置；
	
	1.goodix_touch: 建立3.0 初始版本；
		1.1 支持单点或双点坐标上报方式；
		1.2 采用ctp_platform_ops操作集完成平台相关操作；
		
	ft5x_ts: 建立3.0 初始版本；
		1. 采用ctp_platform_ops操作集完成平台相关操作；