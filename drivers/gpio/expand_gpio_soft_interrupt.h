#ifndef _SOFT_INTERRUPT_H
#define _SOFT_INTERRUPT_H

#define MAX_SUPPORT_PORT_GROUP 5

typedef int (*irq_read_inputreg)(void *,char *);
struct expand_gpio_irq_data
{
	void *data;
	irq_read_inputreg read_allinputreg;
};

struct expand_gpio_global_variable
{
	uint8_t reg_input[MAX_SUPPORT_PORT_GROUP];
	uint8_t reg_output[MAX_SUPPORT_PORT_GROUP];
	uint8_t reg_direction[MAX_SUPPORT_PORT_GROUP];
};
struct expand_gpio_soft_int
{
	unsigned int gpio_irq_start;
	unsigned int irq_pin_num;        				//中断的个数
	unsigned int irq_gpiopin;            			//父中断的中断 脚
	unsigned int irq_chain;            			//父中断的中断号

	unsigned int expand_port_group;
	unsigned int expand_port_pinnum;
	unsigned int rk_irq_mode;
	unsigned int rk_irq_gpio_pull_up_down;
	
	uint8_t interrupt_en[MAX_SUPPORT_PORT_GROUP];		// 0 dis
	uint8_t interrupt_mask[MAX_SUPPORT_PORT_GROUP];		// 0 unmask
	uint8_t inttype_set[MAX_SUPPORT_PORT_GROUP]; 		// Inttype  enable
	uint8_t inttype[MAX_SUPPORT_PORT_GROUP]; 	
	uint8_t inttype1[MAX_SUPPORT_PORT_GROUP];
	
    	struct expand_gpio_irq_data irq_data;
	struct work_struct irq_work;
	struct expand_gpio_global_variable *gvar;
};

extern struct expand_gpio_soft_int expand_irq_data;
extern int wait_untill_input_reg_flash(void);
extern void expand_irq_init(void *data,struct expand_gpio_global_variable *var,irq_read_inputreg handler);

#endif

