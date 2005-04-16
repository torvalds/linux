/*
 *  linux/arch/h8300/kernel/gpio.c
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 */

/*
 * Internal I/O Port Management
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/init.h>

#define _(addr) (volatile unsigned char *)(addr)
#if defined(CONFIG_H83007) || defined(CONFIG_H83068)
#include <asm/regs306x.h>
static volatile unsigned char *ddrs[] = {
	_(P1DDR),_(P2DDR),_(P3DDR),_(P4DDR),_(P5DDR),_(P6DDR),
	NULL,    _(P8DDR),_(P9DDR),_(PADDR),_(PBDDR),
};
#define MAX_PORT 11
#endif

 #if defined(CONFIG_H83002) || defined(CONFIG_H8048)
/* Fix me!! */
#include <asm/regs306x.h>
static volatile unsigned char *ddrs[] = {
	_(P1DDR),_(P2DDR),_(P3DDR),_(P4DDR),_(P5DDR),_(P6DDR),
	NULL,    _(P8DDR),_(P9DDR),_(PADDR),_(PBDDR),
};
#define MAX_PORT 11
#endif

#if defined(CONFIG_H8S2678)
#include <asm/regs267x.h>
static volatile unsigned char *ddrs[] = {
	_(P1DDR),_(P2DDR),_(P3DDR),NULL    ,_(P5DDR),_(P6DDR),
	_(P7DDR),_(P8DDR),NULL,    _(PADDR),_(PBDDR),_(PCDDR),
	_(PDDDR),_(PEDDR),_(PFDDR),_(PGDDR),_(PHDDR),
	_(PADDR),_(PBDDR),_(PCDDR),_(PDDDR),_(PEDDR),_(PFDDR),
	_(PGDDR),_(PHDDR)
};
#define MAX_PORT 17
#endif
#undef _
 
#if !defined(P1DDR)
#error Unsuppoted CPU Selection
#endif

static struct {
	unsigned char used;
	unsigned char ddr;
} gpio_regs[MAX_PORT];

extern char *_platform_gpio_table(int length);

int h8300_reserved_gpio(int port, unsigned int bits)
{
	unsigned char *used;

	if (port < 0 || port >= MAX_PORT)
		return -1;
	used = &(gpio_regs[port].used);
	if ((*used & bits) != 0)
		return 0;
	*used |= bits;
	return 1;
}

int h8300_free_gpio(int port, unsigned int bits)
{
	unsigned char *used;

	if (port < 0 || port >= MAX_PORT)
		return -1;
	used = &(gpio_regs[port].used);
	if ((*used & bits) != bits)
		return 0;
	*used &= (~bits);
	return 1;
}

int h8300_set_gpio_dir(int port_bit,int dir)
{
	int port = (port_bit >> 8) & 0xff;
	int bit  = port_bit & 0xff;

	if (ddrs[port] == NULL)
		return 0;
	if (gpio_regs[port].used & bit) {
		if (dir)
			gpio_regs[port].ddr |= bit;
		else
			gpio_regs[port].ddr &= ~bit;
		*ddrs[port] = gpio_regs[port].ddr;
		return 1;
	} else
		return 0;
}

int h8300_get_gpio_dir(int port_bit)
{
	int port = (port_bit >> 8) & 0xff;
	int bit  = port_bit & 0xff;

	if (ddrs[port] == NULL)
		return 0;
	if (gpio_regs[port].used & bit) {
		return (gpio_regs[port].ddr & bit) != 0;
	} else
		return -1;
}

#if defined(CONFIG_PROC_FS)
static char *port_status(int portno)
{
	static char result[10];
	const static char io[2]={'I','O'};
	char *rp;
	int c;
	unsigned char used,ddr;
	
	used = gpio_regs[portno].used;
	ddr  = gpio_regs[portno].ddr;
	result[8]='\0';
	rp = result + 7;
	for (c = 8; c > 0; c--,rp--,used >>= 1, ddr >>= 1)
		if (used & 0x01)
			*rp = io[ ddr & 0x01];
		else	
			*rp = '-';
	return result;
}

static int gpio_proc_read(char *buf, char **start, off_t offset, 
                          int len, int *unused_i, void *unused_v)
{
	int c,outlen;
	const static char port_name[]="123456789ABCDEFGH";
	outlen = 0;
	for (c = 0; c < MAX_PORT; c++) {
		if (ddrs[c] == NULL)
			continue ;
		len = sprintf(buf,"P%c: %s\n",port_name[c],port_status(c));
		buf += len;
		outlen += len;
	}
	return outlen;
}

static __init int register_proc(void)
{
	struct proc_dir_entry *proc_gpio;

	proc_gpio = create_proc_entry("gpio", S_IRUGO, NULL);
	if (proc_gpio) 
		proc_gpio->read_proc = gpio_proc_read;
	return proc_gpio != NULL;
}

__initcall(register_proc);
#endif

void __init h8300_gpio_init(void)
{
	memcpy(gpio_regs,_platform_gpio_table(sizeof(gpio_regs)),sizeof(gpio_regs));
}
