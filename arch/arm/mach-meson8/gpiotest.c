#include <linux/kernel.h>
#include <mach/gpio.h>
#include <mach/am_regs.h>
#include <linux/delay.h>
struct gpio_chip;
extern int gpio_amlogic_requst_force(struct gpio_chip *chip ,unsigned offset);
extern void   gpio_amlogic_free(struct gpio_chip *chip,unsigned offset);

#define PINMUX_NUM			12
static unsigned pinmux_reg_org_val[PINMUX_NUM];

#define MAX_GPIOX_PIN_NUM		22
#define MAX_GPIOY_PIN_NUM		17
#define MAX_GPIODV_PIN_NUM		30
#define MAX_CARD_PIN_NUM		7
#define MAX_BOOT_PIN_NUM		19
#define MAX_GPIOH_PIN_NUM		10
#define MAX_GPIOZ_PIN_NUM		15
#define MAX_GPIOAO_PIN_NUM		14

struct gpio_group_info_s {
	const char* name;
	const unsigned start_num;
	const unsigned pins_num;
	const unsigned mask;
	const unsigned oen_reg_addr;
	const unsigned oen_start_bit;
	const unsigned output_reg_addr;
	const unsigned output_start_bit;
	const unsigned input_reg_addr;
	const unsigned input_start_bit;
	unsigned oen_org_val;
	unsigned output_org_val;
};

#define GPIO_X_OEN_REG			P_PREG_PAD_GPIO0_EN_N
#define GPIO_X_OUTPUT_REG		P_PREG_PAD_GPIO0_O
#define GPIO_X_INPUT_REG		P_PREG_PAD_GPIO0_I

#define GPIO_Y_OEN_REG			P_PREG_PAD_GPIO1_EN_N
#define GPIO_Y_OUTPUT_REG		P_PREG_PAD_GPIO1_O
#define GPIO_Y_INPUT_REG		P_PREG_PAD_GPIO1_I

#define GPIO_DV_OEN_REG			P_PREG_PAD_GPIO2_EN_N
#define GPIO_DV_OUTPUT_REG		P_PREG_PAD_GPIO2_O
#define GPIO_DV_INPUT_REG		P_PREG_PAD_GPIO2_I

#define GPIO_CARD_OEN_REG		P_PREG_PAD_GPIO0_EN_N
#define GPIO_CARD_OUTPUT_REG		P_PREG_PAD_GPIO0_O
#define GPIO_CARD_INPUT_REG		P_PREG_PAD_GPIO0_I

#define GPIO_BOOT_OEN_REG		P_PREG_PAD_GPIO3_EN_N
#define GPIO_BOOT_OUTPUT_REG		P_PREG_PAD_GPIO3_O
#define GPIO_BOOT_INPUT_REG		P_PREG_PAD_GPIO3_I

#define GPIO_H_OEN_REG			P_PREG_PAD_GPIO3_EN_N
#define GPIO_H_OUTPUT_REG		P_PREG_PAD_GPIO3_O
#define GPIO_H_INPUT_REG		P_PREG_PAD_GPIO3_I

#define GPIO_Z_OEN_REG			P_PREG_PAD_GPIO1_EN_N
#define GPIO_Z_OUTPUT_REG		P_PREG_PAD_GPIO1_O
#define GPIO_Z_INPUT_REG		P_PREG_PAD_GPIO1_I

#define GPIO_AO_OEN_REG			P_AO_GPIO_O_EN_N
#define GPIO_AO_OUTPUT_REG		P_AO_GPIO_O_EN_N
#define GPIO_AO_INPUT_REG		P_AO_GPIO_I


#define GPIO_GROUP_NUM 8

struct gpio_group_info_s g_group_info[GPIO_GROUP_NUM] = {
	{
		.name = "x",
		.start_num = GPIOX_0,
		.pins_num = 22,
		.mask = (1 << 22) - 1,
		.oen_reg_addr = GPIO_X_OEN_REG,
		.oen_start_bit = 0,
		.output_reg_addr = GPIO_X_OUTPUT_REG,
		.output_start_bit = 0,
		.input_reg_addr = GPIO_X_INPUT_REG,
		.input_start_bit = 0,
	}, {
		.name = "y",
		.start_num = GPIOY_0,
		.pins_num = 17,
		.mask = (1 << 17) - 1,
		.oen_reg_addr = GPIO_Y_OEN_REG,
		.oen_start_bit = 0,
		.output_reg_addr = GPIO_Y_OUTPUT_REG,
		.output_start_bit = 0,
		.input_reg_addr = GPIO_Y_INPUT_REG,
		.input_start_bit = 0,
	}, {
		.name = "dv",
		.start_num = GPIODV_0,
		.pins_num = 30,
		.mask = (1 << 30) - 1,
		.oen_reg_addr = GPIO_DV_OEN_REG,
		.oen_start_bit = 0,
		.output_reg_addr = GPIO_DV_OUTPUT_REG,
		.output_start_bit = 0,
		.input_reg_addr = GPIO_DV_INPUT_REG,
		.input_start_bit = 0,
	}, {
		.name = "card",
		.start_num = CARD_0,
		.pins_num = 7,
		.mask = (1 << 7) - 1,
		.oen_reg_addr = GPIO_CARD_OEN_REG,
		.oen_start_bit = 22,
		.output_reg_addr = GPIO_CARD_OUTPUT_REG,
		.output_start_bit = 22,
		.input_reg_addr = GPIO_CARD_INPUT_REG,
		.input_start_bit = 22,
	}, {
		.name = "boot",
		.start_num = BOOT_0,
		.pins_num = 22,
		.mask = (1 << 22) - 1,
		.oen_reg_addr = GPIO_BOOT_OEN_REG,
		.oen_start_bit = 0,
		.output_reg_addr = GPIO_BOOT_OUTPUT_REG,
		.output_start_bit = 0,
		.input_reg_addr = GPIO_BOOT_INPUT_REG,
		.input_start_bit = 0,
	}, {
		.name = "h",
		.start_num = GPIOH_0,
		.pins_num = 10,
		.mask = (1 << 10) - 1,
		.oen_reg_addr = GPIO_H_OEN_REG,
		.oen_start_bit = 19,
		.output_reg_addr = GPIO_H_OUTPUT_REG,
		.output_start_bit = 19,
		.input_reg_addr = GPIO_H_INPUT_REG,
		.input_start_bit = 19,
	}, {
		.name = "z",
		.start_num = GPIOZ_0,
		.pins_num = 15,
		.mask = (1 << 15) - 1,
		.oen_reg_addr = GPIO_Z_OEN_REG,
		.oen_start_bit = 17,
		.output_reg_addr = GPIO_Z_OUTPUT_REG,
		.output_start_bit = 17,
		.input_reg_addr = GPIO_Z_INPUT_REG,
		.input_start_bit = 17,
	}, {
		.name = "ao",
		.start_num = GPIOAO_0,
		.pins_num = 14,
		.mask = (1 << 14) - 1,
		.oen_reg_addr = GPIO_AO_OEN_REG,
		.oen_start_bit = 0,
		.output_reg_addr = GPIO_AO_OUTPUT_REG,
		.output_start_bit = 16,
		.input_reg_addr = GPIO_AO_INPUT_REG,
		.input_start_bit = 0,
	}
};

static struct gpio_group_info_s* get_group_info(char* name)
{
	struct gpio_group_info_s* group_info = NULL;
	int i;
	for (i = 0; i < GPIO_GROUP_NUM; i++) {
		struct gpio_group_info_s* tmp_info;
		tmp_info = &g_group_info[i];
		if(!strcmp(tmp_info->name, name)) {
			group_info = tmp_info;
			break;
		}
	}
	return group_info;
}

#define GPIO_PIN(group_info, i) (group_info->start_num + i)

#define TYPE_INT	0
#define TYPE_HEX	1

static int ssscanf(char *str, int type, unsigned *value)
{
    char *p;
    char c;
    int val = 0;
    p = str;

    c = *p;
    while (!((c >= '0' && c <= '9') || 
             (c >= 'a' && c <= 'f') ||
             (c >= 'A' && c <= 'F'))) {                     // skip other characters 
        p++;
        c = *p;
    }
    switch (type) {
    case TYPE_INT:
        c = *p;
        while (c >= '0' && c <= '9') {
            val *= 10;
            val += c - '0';   
            p++;
            c = *p;
        }
        break;

    case TYPE_HEX:
        if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X')) {
            p += 2;                         // skip '0x' '0X'
        }
        c = *p;
        while ((c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F')) {
            val = val * 16;
            if (c >= '0' && c <= '9') {
                val += c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                val += (c - 'a' + 10);
            }
            if (c >= 'A' && c <= 'F') {
                val += (c - 'a' + 10);
            }
            p++;
            c = *p;
        }
        break;

    default:
        break;
    }

    *value = val;
    return p - str; 
}

static void record_org_pinmux(void)
{
	int i;
	for (i = 0; i < PINMUX_NUM; i++)
		pinmux_reg_org_val[i] = aml_read_reg32(P_PERIPHS_PIN_MUX_0+(i<<2));
}

static void set_gpio_pinmux(struct gpio_group_info_s* g_info, unsigned mask)
{
	int i;
	for (i = 0; i < g_info->pins_num; i++) {
		if (mask & (1 << i))
			gpio_amlogic_requst_force(NULL, GPIO_PIN(g_info, i));
	}
		
}
#if 0
static void free_gpio_pinmux(struct gpio_group_info_s* g_info, unsigned mask)
{
	int i;
	for (i = 0; i < g_info->pins_num; i++) {
		if (mask & (1 << i))
			gpio_amlogic_free(NULL, GPIO_PIN(g_info, i));
	}
		
}
#endif
static void revert_gpiotest_pinmux(void)
{
	int i;
	for (i = 0; i < PINMUX_NUM; i++)
		 aml_write_reg32(P_PERIPHS_PIN_MUX_0+(i<<2), 
		 			pinmux_reg_org_val[i]);
}

#define  TAG "gpio_test "

#define GT_PRK(...) \
do {\
	printk("gpio_test ") ;\
	printk(__VA_ARGS__);\
} while(0)

int gpiotest(int argc, char **argv)
{
	int i, ret;
	unsigned delay_us = 0, repeat_time, mask, total_times = 1;
	unsigned oen_org, output_org;
	unsigned readbak_v;
	unsigned err_val = 0;
	struct gpio_group_info_s* g_info = NULL;
	
	g_info = get_group_info(argv[1]);
	if (g_info == NULL) {
		GT_PRK("gpio group name error!\n");
		GT_PRK("should be one of x, y, dv,"
				 "card, boot, h, z, ao!\n\n");
		return -1;
	}
	
	mask =  g_info->mask;
	
	for (i = 2; i < argc; i += 2) {
		if (!strcmp(argv[i], "-m"))
			ssscanf(argv[i+1], TYPE_HEX, &mask);
		else if (!strcmp(argv[i], "-t"))
			ssscanf(argv[i+1], TYPE_INT, &total_times);
		else if (!strcmp(argv[i], "-d"))
			ssscanf(argv[i+1], TYPE_INT, &delay_us);
		else {
			GT_PRK("args error\n\n");
			return -1;
		}
	}
	
	mask &= g_info->mask;
	
	//GT_PRK("mask %x, delay time %d, repeat count %d\n", mask, delay_us, total_times);
	
	//record the org pinmux value
	record_org_pinmux();
	
	//set the pinmux of gpiotest
	set_gpio_pinmux(g_info, mask);
		
	oen_org = aml_read_reg32(g_info->oen_reg_addr);
	output_org = aml_read_reg32(g_info->output_reg_addr);
	
	//GT_PRK("oen_org = %x, output_org = %x\n", oen_org, output_org);
	
	 aml_write_reg32(g_info->oen_reg_addr, oen_org & (~(mask)));
	
	 aml_write_reg32(g_info->output_reg_addr, output_org | mask);
	//GT_PRK("dir_cur = %x, val_cur = %x\n", 
	//		aml_read_reg32(gpiotest_PIN_DIR_REG), aml_read_reg32(gpiotest_PIN_VAL_REG));
	GT_PRK("test high level\n");
	repeat_time = 0;
	ret = 0;
	do {
		if (delay_us > 0)
			udelay(delay_us);
		readbak_v = aml_read_reg32(g_info->input_reg_addr) & mask;
		
		//GT_PRK("readbak_v = %lx\n", readbak_v);
		
		//for test
		 //   readbak_v &= (~(1 << 5));
		
		for (i = 0; i < g_info->pins_num; i++) {
			if ((mask & (1 << i)) && !(readbak_v& (1 << i))) {
				//GT_PRK("gpiotest%d high level error\n", i);
				if (ret == 0) {
					err_val = readbak_v;
					GT_PRK( "error_val(right_val:0x%x)    time\n"     
						"0x%x                         %d\n", 
							mask, err_val, repeat_time);
				} 
				ret = -1;
			}
			if (readbak_v != err_val && ret != 0) {
				err_val = readbak_v;
				GT_PRK("0x%x                         %d\n", 
					err_val, repeat_time);
			}
		}
	} while (++repeat_time < total_times);
	
	if (ret != -1)
		GT_PRK("always ok!\n");
	
			
	 aml_write_reg32(g_info->output_reg_addr, output_org & (~mask));	
	//GT_PRK("dir_cur = %x, val_cur = %x\n", 
	//		 aml_read_reg32(gpiotest_PIN_DIR_REG), aml_read_reg32(gpiotest_PIN_VAL_REG));
	
	GT_PRK("test low level\n");
	
	repeat_time = 0;
	ret = 0;	
	do {		 
		if (delay_us > 0)
			udelay(delay_us);
			
		readbak_v = aml_read_reg32(g_info->input_reg_addr) & mask;
	
		//GT_PRK("readbak_v = %lx\n", readbak_v);
		
		//for test
		//    readbak_v |= (1 << 5);
		
		for (i = 0; i < g_info->pins_num; i++) {
			if ((mask & (1 << i)) && (readbak_v & (1 << i))) {
				if (ret == 0) {
					err_val = readbak_v;
					GT_PRK( "error_val(right_val:0x%x)    time\n"     
						"0x%x                         %d\n", 
						(~mask) & g_info->mask, err_val, repeat_time);
				} 
				ret = -1;
			}
			if (readbak_v != err_val && ret != 0) {
				err_val = readbak_v;
				GT_PRK("0x%x                           %d\n", 
					err_val, repeat_time);
			}
		}
		
	} while(++repeat_time < total_times);
	if (ret != -1)
		GT_PRK("always ok!\n");
		
	//revert the pinmux value	
	revert_gpiotest_pinmux();
	
	//free_gpio_pinmux(g_info, mask);
		
	//write back all the gpio setting after the test.
	aml_write_reg32(g_info->oen_reg_addr, oen_org);
	aml_write_reg32(g_info->output_reg_addr, output_org);
	 
	return 0;
}
