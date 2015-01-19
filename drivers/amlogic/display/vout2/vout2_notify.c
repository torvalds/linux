/*
 *  linux/drivers/video/apollo/vout_notify.c
 *
 *  Copyright (C) 2009 amlogic
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 * author :   
 *		 jianfeng_wang@amlogic
 */
#include <linux/module.h>
#include <linux/amlogic/vout/vout_notify.h>


static BLOCKING_NOTIFIER_HEAD(vout_notifier_list);
static  DEFINE_MUTEX(vout_mutex)  ;
static  vout_module_t  vout_module={
		.vout_server_list={&vout_module.vout_server_list,&vout_module.vout_server_list},
		.curr_vout_server=NULL,	
};
/**
 *	vout_register_client - register a client notifier
 *	@nb: notifier block to callback on events
 */
int vout2_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&vout_notifier_list, nb);
}
EXPORT_SYMBOL(vout2_register_client);

/**
 *	vout_unregister_client - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int vout2_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&vout_notifier_list, nb);
}
EXPORT_SYMBOL(vout2_unregister_client);

/**
 * vout_notifier_call_chain - notify clients of fb_events
 *
 */
int vout2_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&vout_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(vout2_notifier_call_chain);

/*
*interface export to client who want to get current vinfo.
*/
const vinfo_t *get_current_vinfo2(void)
{
	const vinfo_t *info=NULL;

	mutex_lock(&vout_mutex);
	if(vout_module.curr_vout_server)
	{
		BUG_ON(vout_module.curr_vout_server->op.get_vinfo == NULL);
		info = vout_module.curr_vout_server->op.get_vinfo();
	}
	mutex_unlock(&vout_mutex);

	return info;
}
EXPORT_SYMBOL(get_current_vinfo2);

/*
*interface export to client who want to get current vmode.
*/
vmode_t get_current_vmode2(void)
{
	const vinfo_t *info;
	vmode_t mode=VMODE_MAX;

	mutex_lock(&vout_mutex);

	if(vout_module.curr_vout_server)
	{
		BUG_ON(vout_module.curr_vout_server->op.get_vinfo == NULL);
		info = vout_module.curr_vout_server->op.get_vinfo();
		mode=info->mode;
	}	
	mutex_unlock(&vout_mutex);
	
	return mode;
}
EXPORT_SYMBOL(get_current_vmode2);
int vout2_suspend(void)
{
	int ret=0 ;
	vout_server_t  *p_server = vout_module.curr_vout_server;

	mutex_lock(&vout_mutex);
	if (p_server)
	{
		if(p_server->op.vout_suspend)
		{
			ret = p_server->op.vout_suspend() ;
		}
	}
	
	mutex_unlock(&vout_mutex);
	return ret;
}
EXPORT_SYMBOL(vout2_suspend);
int vout2_resume(void)
{
	vout_server_t  *p_server = vout_module.curr_vout_server;

	mutex_lock(&vout_mutex);
	if (p_server)
	{
		if (p_server->op.vout_resume)
		{
			p_server->op.vout_resume() ; //ignore error when resume.
		}
	}
	
	mutex_unlock(&vout_mutex);
	return 0;
}
EXPORT_SYMBOL(vout2_resume);
/*
*interface export to client who want to set current vmode.
*/
int set_current_vmode2(vmode_t mode)
{
	int r=-1;
	vout_server_t  *p_server;
	
	mutex_lock(&vout_mutex);
	list_for_each_entry(p_server, &vout_module.vout_server_list, list)
	{
		BUG_ON(p_server->op.vmode_is_supported == NULL);
		if(true == p_server->op.vmode_is_supported(mode))
		{
			vout_module.curr_vout_server=p_server;
			r=p_server->op.set_vmode(mode);
			//break;  do not exit , should disable other modules
		}
		else
		{
			//p_server->op.disable(mode);
		}
	}
	
	mutex_unlock(&vout_mutex);

	return r;
}
EXPORT_SYMBOL(set_current_vmode2);

/*
*interface export to client who want to set current vmode.
*/
vmode_t validate_vmode2(char *name)
{
	vmode_t r=VMODE_MAX;
	vout_server_t  *p_server;
	
	mutex_lock(&vout_mutex);
	list_for_each_entry(p_server, &vout_module.vout_server_list, list)
	{
		BUG_ON(p_server->op.validate_vmode == NULL);
		r = p_server->op.validate_vmode(name);
		if(VMODE_MAX != r) //valid vmode find.
		{
			break;
		}
	}
	mutex_unlock(&vout_mutex);

	return r;
}
EXPORT_SYMBOL(validate_vmode2);

/*
*here we offer two functions to get and register vout module server
*vout module server will set and store tvmode attributes for vout encoder
*we can ensure TVMOD SET MODULE independent with these two function.
*/


int vout2_register_server(vout_server_t*  mem_server)
{
	list_head_T  *p_iter;
	vout_server_t  *p_server;

	BUG_ON(mem_server == NULL);
	mutex_lock(&vout_mutex);
	list_for_each(p_iter,&vout_module.vout_server_list )
	{
		p_server=list_entry(p_iter,vout_server_t,list);
		if(p_server->name && mem_server->name && strcmp(p_server->name,mem_server->name)==0)
		{
			//vout server already registered.
			
			mutex_unlock(&vout_mutex);
			return -1;
		}
	}
	list_add(&mem_server->list,&vout_module.vout_server_list);
	mutex_unlock(&vout_mutex);
	return  0 ;
}
EXPORT_SYMBOL(vout2_register_server);
int vout2_unregister_server(vout_server_t*  mem_server)
{
	vout_server_t  *p_server;

	BUG_ON(mem_server == NULL);
	mutex_lock(&vout_mutex);
	list_for_each_entry(p_server, &vout_module.vout_server_list, list)
	{
		if(p_server->name && mem_server->name && strcmp(p_server->name,mem_server->name)==0)
		{
			//we will not move current vout server pointer automatically if current vout server
			//pointer is the one which will be deleted next .so you should change current vout server 
			//first then remove it .
			if(vout_module.curr_vout_server==p_server)
			vout_module.curr_vout_server=NULL;
			
			list_del(&mem_server->list);
			mutex_unlock(&vout_mutex);
			return 0;
		}
	}
	mutex_unlock(&vout_mutex);
	return 0;
}
EXPORT_SYMBOL(vout2_unregister_server);
