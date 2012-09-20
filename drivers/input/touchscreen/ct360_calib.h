
struct ct360_ts_data {
	u16		x_max;	
	u16		y_max;
	bool	swap_xy;           //define?
	int 	irq;
	struct 	i2c_client *client;
    struct 	input_dev *input_dev;
	struct workqueue_struct *ct360_wq;
    struct 	work_struct  work;
    struct 	early_suspend early_suspend;
};
