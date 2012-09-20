
struct ct36x_i2c_data {
	unsigned char				buf[32];
};


struct ct360_ts_data {
	u16		x_max;	
	u16		y_max;
	bool	swap_xy;           //define?
	int 	irq;
	struct ct36x_i2c_data	data;
	struct 	i2c_client *client;
    struct 	input_dev *input_dev;
	struct workqueue_struct *ct360_wq;
    struct 	work_struct  work;
    struct 	early_suspend early_suspend;
};
