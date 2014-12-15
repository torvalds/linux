#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/of.h>
#include "key_manage.h"

#define UNIFYKEY_DATAFORMAT_HEXDATA	"hexdata"
#define UNIFYKEY_DATAFORMAT_HEXASCII	"hexascii"
#define UNIFYKEY_DATAFORMAT_ALLASCII	"allascii"

#define UNIFYKEY_DEVICE_EFUSEKEY	"efusekey"
#define UNIFYKEY_DEVICE_NANDKEY		"nandkey"
#define UNIFYKEY_DEVICE_SECURESKEY	"secureskey"

#define UNIFYKEY_PERMIT_READ		"read"
#define UNIFYKEY_PERMIT_WRITE		"write"
#define UNIFYKEY_PERMIT_DEL			"del"

static struct key_info_t unify_key_info={.key_num =0, .key_flag = 0};
static struct key_item_t *unifykey_item=NULL;

int unifykey_item_verify_check(struct key_item_t *key_item)
{
	if(!key_item){
		printk("%s:%d unify key item is invalid\n",__func__,__LINE__);
		return -1;
	}
	if((key_item->dev == KEY_M_UNKNOW_DEV) ||(key_item->df == KEY_M_MAX_DF)){
		printk("%s:%d unify key item is invalid\n",__func__,__LINE__);
		return -1;
	}
	return 0;
}

struct key_item_t *unifykey_find_item_by_name(char *name)
{
	struct key_item_t *pre_item;
	pre_item = unifykey_item;
	while(pre_item){
		if(!strncmp(pre_item->name,name,strlen(pre_item->name))){
			return pre_item;
		}
		pre_item = pre_item->next;
	}
	return NULL;
}
struct key_item_t *unifykey_find_item_by_id(int id)
{
	struct key_item_t *pre_item;
	pre_item = unifykey_item;
	while(pre_item){
		if(pre_item->id == id){
			return pre_item;
		}
		pre_item = pre_item->next;
	}
	return NULL;
}

int unifykey_count_key(void)
{
	int count=0;
	struct key_item_t *pre_item;
	pre_item = unifykey_item;
	while(pre_item){
		count++;
		pre_item = pre_item->next;
	}
	return count;
}
char unifykey_get_efuse_version(void)
{
	char ver=0;
	if(unify_key_info.efuse_version != -1){
		ver = (char)unify_key_info.efuse_version;
	}
	return ver;
}

static int unifykey_create_list(struct key_item_t *item)
{
	struct key_item_t *pre_item;
	if(unifykey_item == NULL){
		unifykey_item = item;
	}
	else{
		pre_item = unifykey_item;
		while(pre_item->next != NULL){
			pre_item = pre_item->next;
		}
		pre_item->next = item;
	}
	return 0;
}
static int unifykey_free_list(void)
{
	struct key_item_t *pre_item;
	pre_item = unifykey_item;
	while(pre_item){
		unifykey_item = unifykey_item->next;
		kfree(pre_item);
		pre_item = unifykey_item;
	}
	return 0;
}

static int unifykey_item_parse_dt(struct device_node *node,int id)
{
	int count;
	int ret=-1;
	const char *propname;
	struct key_item_t *temp_item=NULL;

	temp_item = kzalloc(sizeof(struct key_item_t), GFP_KERNEL);
	if(!temp_item){
		printk("%s:%d,no memory for key_item\n",__func__,__LINE__);
		ret = -ENOMEM;
		return ret;
	}
	propname = NULL;
	ret = of_property_read_string(node,"key-name",&propname);
	if(ret < 0){
		printk("%s:%d,get key-name fail\n",__func__,__LINE__);
		ret = -EINVAL;
		goto exit;
	}
	if(propname){
		//temp_item->name = kzalloc(strlen(propname)+1, GFP_KERNEL);
		//if(!temp_item->name){
		//	printk("%s:%d,no memory for key_item\n",__func__,__LINE__);
		//	ret = -ENOMEM;
		//	goto exit;
		//}
		count = strlen(propname);
		memset(temp_item->name,0,KEY_UNIFY_NAME_LEN);
		//memcpy(temp_item->name,propname,strlen(propname));
		if(count >=KEY_UNIFY_NAME_LEN){
			count = KEY_UNIFY_NAME_LEN-1;
		}
		strncpy(temp_item->name,propname,count);
		//strcpy(temp_item->name,propname);
	}
	
	propname = NULL;
	ret = of_property_read_string(node,"key-device",&propname);
	if(ret < 0){
		printk("%s:%d,get key-device fail\n",__func__,__LINE__);
		ret = -EINVAL;
		goto exit;
	}
	if(propname){
		if(strcmp(propname,UNIFYKEY_DEVICE_EFUSEKEY) == 0){
			temp_item->dev = KEY_M_EFUSE_NORMAL;
		}
		else if(strcmp(propname,UNIFYKEY_DEVICE_NANDKEY) == 0){
			temp_item->dev = KEY_M_GENERAL_NANDKEY;
		}
		else if(strcmp(propname,UNIFYKEY_DEVICE_SECURESKEY) == 0){
			temp_item->dev = KEY_M_SECURE_STORAGE;
		}
		else{
			temp_item->dev = KEY_M_UNKNOW_DEV;
		}
	}
	propname = NULL;
	ret = of_property_read_string(node,"key-dataformat",&propname);
	if(ret < 0){
		printk("%s:%d,get key-dataformat fail\n",__func__,__LINE__);
		ret = -EINVAL;
		goto exit;
	}
	if(propname){
		if(strcmp(propname,UNIFYKEY_DATAFORMAT_HEXDATA) == 0){
			temp_item->df = KEY_M_HEXDATA;
		}
		else if(strcmp(propname,UNIFYKEY_DATAFORMAT_HEXASCII) == 0){
			temp_item->df = KEY_M_HEXASCII;
		}
		else if(strcmp(propname,UNIFYKEY_DATAFORMAT_ALLASCII) == 0){
			temp_item->df = KEY_M_ALLASCII;
		}
		else{
			temp_item->df = KEY_M_MAX_DF;
		}
	}
	temp_item->permit = 0;
	if(of_property_match_string(node, "key-permit", UNIFYKEY_PERMIT_READ)>=0){
		temp_item->permit |= KEY_M_PERMIT_READ;
	}
	if(of_property_match_string(node, "key-permit", UNIFYKEY_PERMIT_WRITE)>=0){
		temp_item->permit |= KEY_M_PERMIT_WRITE;
	}
	if(of_property_match_string(node, "key-permit", UNIFYKEY_PERMIT_DEL)>=0){
		temp_item->permit |= KEY_M_PERMIT_DEL;
	}
	temp_item->id = id;
	unifykey_create_list(temp_item);
	return 0;
exit:
	if(temp_item){
		kfree(temp_item);
	}
	return ret;
}



static int unifykey_item_create(struct platform_device *pdev,int num)
{
	int ret=-1;
	int index;
	struct device_node *child;
	struct device_node *np = pdev->dev.of_node;
#if 0
	phandle phandle;
	const char *propname;
	const __be32 *list;
	struct property *prop;
	int size,config,count=0;
	of_node_get(np);
	for(index=0;index<num;index++){
		propname = kasprintf(GFP_KERNEL, "unifykey-index-%d", index);
		prop = of_find_property(np, propname, &size);
		kfree(propname);
		if (!prop)
			break;
		list = prop->value;
		size /= sizeof(*list);
		for(config=0;config<size;config++){
			phandle = be32_to_cpup(list++);
			child = of_find_node_by_phandle(phandle);
			if(!child){
				printk("%s:%d,child device_node is not exist\n",__func__,__LINE__);
				break;
			}
			ret = unifykey_item_parse_dt(child,count);
			if(!ret){
				count++;
			}
			of_node_put(child);
		}
	}
	printk("key unify fact unifykey-num is %d\n",count);
#else
	of_node_get(np);
	index = 0;
	for_each_child_of_node(np,child){
		ret = unifykey_item_parse_dt(child,index);
		if(!ret){
			index++;
		}
	}
	printk("key unify fact unifykey-num is %d\n",index);
#endif
	return 0;
}

int unifykey_dt_create(struct platform_device *pdev)
{
	int ret=-1;
	int key_num;
	if (pdev->dev.of_node) {
		ret = of_property_read_u32(pdev->dev.of_node,"unifykey-num",&key_num);
		if(ret){
			printk("%s:%d,don't find to match unifykey-num\n",__func__,__LINE__);
			return ret;
		}
		unify_key_info.efuse_version = -1;
		of_property_read_u32(pdev->dev.of_node,"efuse-version",&unify_key_info.efuse_version);
		printk("key unify config unifykey-num is %d\n",key_num);
		unify_key_info.key_num = key_num;
		if(!unify_key_info.key_flag){
			unifykey_item_create(pdev,key_num);
			unify_key_info.key_flag = 1;
		}
	}
	return ret;
}
int unifykey_dt_release(struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		of_node_put(pdev->dev.of_node);
	}
	unifykey_free_list();
	unify_key_info.key_flag = 0;
	return 0;
}

#if 0
//uboot
static int unifykey_item_create(unsigned int  dt_addr,int num)
{
#ifdef CONFIG_OF_LIBFDT
	int i,nodeoffset,count=0;
	char item_path[100];
	char cha[10];
	char *propdata,*p;
	struct fdt_property *prop;
	struct key_item_t *temp_item=NULL;
	
	memset(item_path,0,sizeof(item_path));
	memset(cha,0,sizeof(cha));
	for(i=0;i<num;i++){
		strcpy(item_path,"/unifykey/key_");
		sprintf(cha,"%d",i);
		strcat(item_path,cha);
		
		nodeoffset = fdt_path_offset(dt_addr, item_path);
		if(nodeoffset < 0) {
			printf(" dts: not find  node %s.\n",fdt_strerror(nodeoffset));
			break;
		}
		
		temp_item = malloc(sizeof(struct key_item_t));
		if(!temp_item){
			printf("malloc mem fail,%s:%d\n",__func__,__LINE__);
			return -ENOMEM;
		}
		memset(temp_item,0,sizeof(struct key_item_t));
		propdata = fdt_getprop(dt_addr, nodeoffset, "key-name",NULL);
		if(!propdata){
			printf("%s get key-name fail,%s:%d\n",item_path,__func__,__LINE__);
			break;
		}
		temp_item->name = malloc(strlen(propdata)+1);
		if(!temp_item->name){
			printf("malloc mem fail,%s:%d\n",__func__,__LINE__);
			return -ENOMEM;
		}
		memset(temp_item->name,0,strlen(propdata)+1);
		strcpy(temp_item->name,propdata);
		propdata = fdt_getprop(dt_addr, nodeoffset, "key-device",NULL);
		if(!propdata){
			printf("%s get key-device fail,%s:%d\n",item_path,__func__,__LINE__);
			break;
		}
		if(propdata){
			if(strcmp(propdata,UNIFYKEY_DEVICE_EFUSEKEY) == 0){
				temp_item->dev = KEY_M_EFUSE_NORMAL;
			}
			else if(strcmp(propdata,UNIFYKEY_DEVICE_NANDKEY) == 0){
				temp_item->dev = KEY_M_GENERAL_NANDKEY;
			}
			else if(strcmp(propdata,UNIFYKEY_DEVICE_SECURESKEY) == 0){
				temp_item->dev = KEY_M_SECURE_STORAGE;
			}
			else{
				temp_item->dev = KEY_M_UNKNOW_DEV;
			}
		}
		propdata = fdt_getprop(dt_addr, nodeoffset, "key-dataformat",NULL);
		if(!propdata){
			printf("%s get key-dataformat fail,%s:%d\n",item_path,__func__,__LINE__);
			break;
		}
		if(propdata){
			if(strcmp(propdata,UNIFYKEY_DATAFORMAT_HEXDATA) == 0){
				temp_item->df = KEY_M_HEXDATA;
			}
			else if(strcmp(propdata,UNIFYKEY_DATAFORMAT_HEXASCII) == 0){
				temp_item->df = KEY_M_HEXASCII;
			}
			else if(strcmp(propdata,UNIFYKEY_DATAFORMAT_ALLASCII) == 0){
				temp_item->df = KEY_M_ALLASCII;
			}
			else{
				temp_item->df = KEY_M_MAX_DF;
			}
		}
		//propdata = fdt_getprop(dt_addr, nodeoffset, "key-permit",NULL);
		//if(!propdata){
		//	printf("%s get key-permit fail,%s:%d\n",item_path,__func__,__LINE__);
		//	break;
		//}
		prop = fdt_get_property(dt_addr,nodeoffset,"key-permit",NULL);
		if(!prop){
			printf("%s get key-permit fail,%s:%d\n",item_path,__func__,__LINE__);
			break;
		}
		if(prop){
			temp_item->permit = 0;
			if(fdt_stringlist_contains(prop->data, prop->len, UNIFYKEY_PERMIT_READ)){
				temp_item->permit |= KEY_M_PERMIT_READ;
			}
			if(fdt_stringlist_contains(prop->data, prop->len, UNIFYKEY_PERMIT_WRITE)){
				temp_item->permit |= KEY_M_PERMIT_WRITE;
			}
			if(fdt_stringlist_contains(prop->data, prop->len, UNIFYKEY_PERMIT_DEL)){
				temp_item->permit |= KEY_M_PERMIT_DEL;
			}
		}
		temp_item->id = i;
		unifykey_create_list(temp_item);
		temp_item = NULL;
		memset(item_path,0,sizeof(item_path));
		memset(cha,0,sizeof(cha));
		count++;
	}
	if(temp_item){
		if(temp_item->name){
			free(temp_item->name);
		}
		free(temp_item);
	}
	printf("unifykey-num fact is %x\n",count);
#endif
	return 0;
}

int unifykey_dt_parse(void)
{
#ifdef CONFIG_OF_LIBFDT
	int nodeoffset;
	unsigned int  dt_addr;
	char *punifykey_num;

	if (getenv("dtbaddr") == NULL) {
		dt_addr = CONFIG_DTB_LOAD_ADDR;
	}
	else {
		dt_addr = simple_strtoul (getenv ("dtbaddr"), NULL, 16);
	}
	
	if(fdt_check_header((void*)dt_addr)!= 0){
        printf(" error: image data is not a fdt\n");
        return -1;
    }
	nodeoffset = fdt_path_offset(dt_addr, "/unifykey");
	if(nodeoffset < 0) {
		printf(" dts: not find  node %s.\n",fdt_strerror(nodeoffset));
		return -1;
	}
	punifykey_num = fdt_getprop(dt_addr, nodeoffset, "unifykey-num",NULL);
	printf("unifykey-num config is %x\n",be32_to_cpup((unsigned int*)punifykey_num));
	unify_key_info.key_num = be32_to_cpup((unsigned int*)punifykey_num);
	
	if(!unify_key_info.key_flag){
		unifykey_item_create(dt_addr,unify_key_info.key_num);
		unify_key_info.key_flag = 1;
	}
#endif
	return 0;
}
#endif


