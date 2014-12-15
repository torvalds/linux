/*
 *
 * arch/arm/plat-meson/debug.c
 *
 * Copyright (C) 2010 Amlogic Inc.
 * Copyright (C) 2011 Amlogic Inc.
 *
 *	2011 Victor Wan <victor.wan@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


/* Standard Linux headers */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wakelock.h>

/* Amlogic headers */
#include <plat/io.h>
#include <mach/am_regs.h>
#include <mach/clock.h>
#include <mach/cpu.h>

#define CLASS_NAME        "amlogic"
/* path name:		"/sys/class/CLASS_NAME/" */
enum{
	WORK_MODE_READ,
	WORK_MODE_WRITE,
	WORK_MODE_CLKMEASURE,
	WORK_MODE_DUMP,
	WORK_MODE_THREAD,
	WORK_MODE_STACK,
	WORK_MODE_GPIOTEST,
};
static const char * usage_str =
{"Usage:\n"
"    echo [read | write <data>] addrmem > debug ; Access memory address\n"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
"    echo [read | write <data>] [c | v | a | x | d | s | m] addr > debug ; Access CBUS/VCBUS/AOBUS/AXBUX/DOS/SECBUS/MMC logic address\n"
"    echo dump [c | v | a | x | d | s | m] <start> <end> > debug ; Dump CBUS/VCBUS/AOBUS/AXBUS/DOS/SECBUS/MMC address from <start> to <end>\n"
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
"    echo [read | write <data>] [c | a | x | d | s] addr > debug ; Access CBUS/AOBUS/AXBUX/DOS/SECBUS logic address\n"
"    echo dump [c | a | x | d | s] <start> <end> > debug ; Dump CBUS/AOBUS/AXBUS/DOS address from <start> to <end>\n"
#else
"    echo [read | write <data>] [c | a | x] addr > debug ; Access CBUS/AOBUS/AXBUS logic address\n"
"    echo dump [c | a | x] <start> <end> > debug ; Dump CBUS/AOBUS/AXBUS address from <start> to <end>\n"
#endif // CONFIG_ARCH_MESON6
"    echo clkmsr {<index>} > debug ; Output clk source value, no index then all\n"
"    echo thread {<pid>} > debug; Show thread infomation, no pid then all\n"
"    echo stack <pid> > debug; Show thread's stack\n"
#ifdef CONFIG_GPIO_TEST	
"    echo gpiotest [name] <-m mask> <-t times> <-d delay_us>\n"
#endif // CONFIG_GPIO_TEST	
"\n"
"Address format:\n"
"    addrmem : 0xXXXXXXXX, 32 bits physical address\n"
"    addr    : 0xXXXX, 16 bits register address\n"
};
static const char * 	syntax_error_str = {"Invalid syntax\n"};

static ssize_t dbg_do_help(struct class *class,
			struct class_attribute *attr,	char *buf)
{
	return sprintf(buf, "%s\n",usage_str);
}
static char * base_addr_type(char base)
{
	switch(base){
	case '0':
		return "MEMORY";
	case 'c':
	case 'C':
		return "CBUS";
	case 'a':
	case 'A':
		return "AOBUS";
	case 'x':
	case 'X':
		return "AXBUS";
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	case 'd':
	case 'D':
		return "DOS";
	case 's':
	case 'S':
		return "SECBUS";
#endif // CONFIG_ARCH_MESON6
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	case 'v':
        case 'V':
                return "VCBUS";
	case 'm':
	case 'M':
                return "MMC";
#endif // CONFIG_ARCH_MESON8
	default:
		break;
	}
	return "INVALID";
}
static unsigned int  base_addr_convert(char base,unsigned int address)
{
	if (base != '0' && address > 0xffff)
		return -1;

	switch(base){
	case 'c':
	case 'C':
		address = CBUS_REG_ADDR(address);
		break;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON3
	case 'a':
	case 'A':
		address = AOBUS_REG_ADDR(address);
		break;
#endif
	case 'x':
	case 'X':
		address = AXI_REG_ADDR(address);
		break;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	case 'd':
	case 'D':
		address = DOS_REG_ADDR(address);
		break;
	case 's':
	case 'S':
		address = SECBUS_REG_ADDR(address);
		break;
#endif // CONFIG_ARCH_MESON6
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	case 'v':
	case 'V':
		address = VCBUS_REG_ADDR(address);
		break;
        case 'm':
        case 'M':
		address = MMC_REG_ADDR(address);
		break;
#endif // CONFIG_ARCH_MESON8
	default:
		break;
	}
	return address;
}

/* Command Works */
static int  do_read_work(char argn ,char **argv)
{
	char base ;
	char *type = NULL;
	unsigned int address,vir_addr,value;

	if (argn < 2){
		printk("%s",syntax_error_str);
		return -1;
	}

	base = argv[1][0];
	if (base == '0' || argn == 2){
		address = simple_strtol(argv[1],NULL,16);
		base = '0';
	}
	else
		address = simple_strtol(argv[2],NULL,16);

	type = base_addr_type(base);
	vir_addr = base_addr_convert(base, address);
	if (vir_addr == -1){
		printk("%s[0x%04x] Invalid Address\n", type, address);
		return -1;
	}
	value = aml_read_reg32(vir_addr);
	if (base == '0')
		printk("%s[0x%08x]=0x%08x\n", type, vir_addr, value);
	else
		printk("%s[0x%04x]=0x%08x\n", type, address, value);
	return 0;
}
static int  do_dump_work(char argn ,char **argv)
{
	char base;
	char *type = NULL;
	unsigned int start, end, vstart, value;

	if (argn < 4){
		printk("%s",syntax_error_str);
		return -1;
	}

	base = argv[1][0];
	type = base_addr_type(base);
	start = simple_strtol(argv[2], NULL, 16);
	end = simple_strtol(argv[3], NULL, 16);

	do {
		vstart = base_addr_convert(base, start);
		if (vstart == -1)
			printk("%s[0x%04x] Invalid Address\n", type, start);
		else	{
			value = aml_read_reg32(vstart);
			printk("%s[0x%04x]=0x%08x\n", type, start, value);
		}
		start++;
	} while (start <= end);
	return 0;
}

int do_clk_measure_work(char argn ,char **argv )
{
	if(argn == 1){
		clk_measure(0xff);
	}else{
		clk_measure(simple_strtol(argv[1],NULL,10));
	}
	return 0;
}

static int do_write_work(char argn ,char **argv)
{
	char base ;
	char *type = NULL;
	unsigned int address,vir_addr,value;

	if (argn < 3){
		printk("%s",syntax_error_str);
		return -1;
	}

	base = argv[2][0];
	if (base == '0' || argn == 3){
		address = simple_strtol(argv[2], NULL, 16);
		base = '0';
	}
	else
		address = simple_strtol(argv[3], NULL, 16);

	type = base_addr_type(base);
	vir_addr = base_addr_convert(base,address);
	if (vir_addr == -1)	{
		printk("%s[0x%04x] Invalid Address\n", type, address);
		return -1;
	}
	value = simple_strtol(argv[1], NULL, 16);
	aml_write_reg32(vir_addr, value);

	if (base == '0')
		printk("Write %s[0x%08x]=0x%08x\n", type, vir_addr, aml_read_reg32(vir_addr));
	else
		printk("Write %s[0x%04x]=0x%08x\n", type, address, aml_read_reg32(vir_addr));
	return 0;
}

int do_thread_show_work(char argn ,char **argv )
{
	struct task_struct *p;
	int pid;

	if(argn != 1 && argn != 2){
		printk("%s",syntax_error_str);
		return 0;
	}

	printk("pid:	state:	task:	name:\n");
	if(argn == 1){
		for_each_process(p) {
			pid = pid_vnr(get_task_pid(p,PIDTYPE_PID));
			printk("%4d: \t%ld\t%p\t%s\n",pid,p->state,p,p->comm);
		} 
	}else{
		pid = simple_strtol(argv[1],NULL,10);
		p = pid_task(find_vpid(pid), PIDTYPE_PID);
		printk("%5d: \t%ld\t%p\t%s\n",pid,p->state,p,p->comm);
	}
	return 0;
}
int do_stack_show_work(char argn ,char **argv )
{
	pid_t pid;
	struct task_struct *task;
		
	if(argn != 2){
		printk("%s",syntax_error_str);
	}else{
		pid = simple_strtol(argv[1],NULL,10);
		rcu_read_lock();
		task = pid_task(find_vpid(pid), PIDTYPE_PID);
		rcu_read_unlock();
		if(task)
			sched_show_task(task);
		else
			printk("PID %d not found\n",pid);
	}
	return 0;
}

#ifdef CONFIG_GPIO_TEST
static struct wake_lock	debug_lock;
extern int gpiotest(int argc, char **argv);

#define MAX_ARG_NUM 8

int do_gpio_test_work(char argn ,char **argv )
{
	//int i;
	if(argn < 2 || argn > MAX_ARG_NUM){
		printk("%s",syntax_error_str);
	}else{
		wake_lock(&debug_lock);
		//for (i = 0; i < 10000; i++)
			//printk("arg[%d] = %s\n", i, argv[i]);
		gpiotest(argn, argv);
		wake_unlock(&debug_lock);
	}
	return 0;
}
#else 

#define MAX_ARG_NUM 4

#endif /*CONFIG_GPIO_TEST*/



/* Main Command Dispatcher */
static ssize_t dbg_do_command(struct class *class,
			struct class_attribute *attr,	const char *buf, size_t count)
{

	int argn;
	char * buf_work,*p,*para;
	char * argv[MAX_ARG_NUM];
	char cmd,work_mode;

	buf_work = kstrdup(buf, GFP_KERNEL);
	p = buf_work;

	for(argn = 0; argn < MAX_ARG_NUM; argn++){
		para = strsep(&p," ");
		if(para == NULL)
			break;
		argv[argn] = para;
		//printk("argv[%d] = %s\n",argn,para);
	}

	if(argn < 1 || argn > MAX_ARG_NUM)
		goto end;

	cmd = argv[0][0];
	switch (cmd){
	case 'r':
	case 'R':
		work_mode=WORK_MODE_READ;
		do_read_work(argn,argv);
		break;
	case 'w':
	case 'W':
		work_mode=WORK_MODE_WRITE;
		do_write_work(argn,argv);
		break;
	case 'c':
	case 'C':
		work_mode=WORK_MODE_CLKMEASURE;
		do_clk_measure_work(argn,argv);
		break;
	case 'd':
	case 'D':
		work_mode=WORK_MODE_DUMP;
		do_dump_work(argn,argv);
		break;
	case 't':
	case 'T':
		work_mode=WORK_MODE_THREAD;
		do_thread_show_work(argn,argv);
		break;
	case 's':
	case 'S':
		work_mode=WORK_MODE_STACK;
		do_stack_show_work(argn,argv);
		break;

#ifdef CONFIG_GPIO_TEST	
	case 'g':
	case 'G':
		work_mode=WORK_MODE_GPIOTEST;
		do_gpio_test_work(argn,argv);
		break;
#endif /* CONFIG_GPIO_TEST */

	default:
		goto end;
	}
	
	kfree(buf_work);
	return count;
end:
	printk("error command!\n");
	kfree(buf_work);
	return -EINVAL;

}

static CLASS_ATTR(debug,S_IWUSR | S_IRUGO, dbg_do_help, dbg_do_command);
static CLASS_ATTR(help,S_IWUSR | S_IRUGO, dbg_do_help, NULL);

struct class * aml_sys_class;
EXPORT_SYMBOL(aml_sys_class);
static int __init aml_debug_init(void)
{
	int ret = 0;

	aml_sys_class = class_create(THIS_MODULE,CLASS_NAME);

	ret = class_create_file(aml_sys_class,&class_attr_debug);
	ret = class_create_file(aml_sys_class,&class_attr_help);
	
#ifdef CONFIG_GPIO_TEST		
	wake_lock_init(&debug_lock, WAKE_LOCK_SUSPEND, "gpiotest");
#endif /* CONFIG_GPIO_TEST */

	return ret;
}

static void __exit aml_debug_exit(void)
{
#ifdef CONFIG_GPIO_TEST		
	wake_lock_destroy(&debug_lock);
#endif /* CONFIG_GPIO_TEST */

	class_destroy(aml_sys_class);
}


module_init(aml_debug_init);
module_exit(aml_debug_exit);

MODULE_DESCRIPTION("Amlogic debug module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Wan <victor.wan@amlogic.com>");

