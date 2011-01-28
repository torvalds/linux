/*
 * kobj.c
 *
 * (C) Copyright hsl 2009
 *	Released under GPL v2.
 *
 * 
 * log:
 *      for debug function by sysfs 
 *      
 */


#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/wait.h>

#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/timer.h>
#include <linux/spinlock_types.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>
#include <linux/rtc.h>
#include <asm/atomic.h>
#include <asm/signal.h>

static struct kobject *dbg_kobj;

extern int dbg_parse_cmd( char * cdb );

static char     dbg_lastcmd[512];

/* default kobject attribute operations */
static ssize_t dbg_call_attr_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
        strcpy( buf , dbg_lastcmd );
        strcat(buf,"\n");
        return strlen(buf)+1;
}
static ssize_t dbg_call_attr_store(struct kobject *kobj, struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
        char    cmd[512];
        int len = sizeof(dbg_lastcmd)>count ? count:sizeof(dbg_lastcmd);
        //printk("count=%d,cmd=%s!\n" , count , buf );
        strncpy( dbg_lastcmd , buf , len );
        if( dbg_lastcmd[len-1] == '\n' ){
            dbg_lastcmd[len-1] = 0;
            len--;
        }
        strncpy( cmd , dbg_lastcmd , sizeof(cmd) );
         dbg_parse_cmd( cmd );
        return count;
}



static struct kobj_attribute _call_attr = {	
	.attr	= {				
		.name = "dbg",	
		.mode = 0644,			
	},					
	.show	= dbg_call_attr_show,			
	.store	= dbg_call_attr_store,		
};

static struct attribute * g[] = {
	&_call_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};


int dbg_init( void )
{
        //dbg_kobj = kobject_create_and_add("dbg", NULL ); /*kernel_kobj*/
        //if (!dbg_kobj)
        //        return -ENOMEM;
        //return sysfs_create_group(dbg_kobj, &attr_group);
        return sysfs_create_group(kernel_kobj, &attr_group);
}

late_initcall(dbg_init);

