#include "disp_sprite.h"
#include "disp_display.h"
#include "disp_layer.h"
#include "disp_event.h"

static sprite_t gsprite;

static __s32 Sprite_Get_Idle_Block_id(void)
{
    __s32 i = 0;

    for(i = 0;i<MAX_SPRITE_BLOCKS;i++)
    {
        if(!(gsprite.block_status[i] & SPRITE_BLOCK_USED))
        {
            return i;
        }
    }
    return (__s32)DIS_NO_RES;
}

static __s32 Sprite_Id_To_Hid(__s32 id)
{
	if(id == -1)
	{
		return 0;
	}
	else
	{
		return gsprite.sprite_hid[id];
	}
}

static __s32 Sprite_Hid_To_Id(__s32 hid)
{
	if(hid == 0)
	{
		return -1;
	}
	else
	{
		__s32 i =0;
		for(i=0;i<MAX_SPRITE_BLOCKS;i++)
		{
			if(gsprite.sprite_hid[i] == hid)
			{
				return i;
			}
		}
		return -1;
	}
}

//--hgl--用这个的前提：prev,next必须是存在的，否则崩溃。
static __inline void ___list_add(list_head_t *node,list_head_t *prev,list_head_t *next)
{
	node->next = next;
	node->prev = prev;
	prev->next = node;
	next->prev = node;
}

//将node添加到list的最后面，也既其前面
static  __inline void list_add_node_tail(list_head_t *node, list_head_t **head)
{
	if(*head == NULL)
	{
		*head = node;
	}
	else
	{
		___list_add(node, (*head)->prev, *head);
	}
}

//从list中删除entry
static __inline void list_del_node(list_head_t *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	entry->next = entry;
	entry->prev = entry;
}

//内部函数,释放该节点的空间
static __inline void list_free_node(list_head_t * node)
{
	if(node != NULL)
	{
		OSAL_free(node->data);
		OSAL_free(node);
		node = NULL;
	}
}

//申请一个新的结点,并初始化
static list_head_t * List_New_Sprite_Block(__disp_sprite_block_para_t * para)
{
	list_head_t * node = NULL;
	sprite_block_data_t * data = NULL;
	__s32 id;

	id = Sprite_Get_Idle_Block_id();

	if(id != DIS_NO_RES)
	{
		data = OSAL_malloc(sizeof(sprite_block_data_t));
		data->enable = FALSE;
		data->id = id;
		data->src_win.x = para->src_win.x;
		data->src_win.y = para->src_win.y;
		data->scn_win.x = para->scn_win.x;
		data->scn_win.y = para->scn_win.y;
		data->scn_win.width = para->scn_win.width;
		data->scn_win.height = para->scn_win.height;
		data->address = (__u32)para->fb.addr[0];
		data->size.width = para->fb.size.width;

		node = OSAL_malloc(sizeof(list_head_t));
		node->next = node->prev = node;
		node->data = data;

		return node;
	}
	else
	{
		return NULL;
	}
}

//在链表的尾部增加新结点
static void* List_Add_Sprite_Block(__disp_sprite_block_para_t * para)
{
	list_head_t * node = NULL;

	node = List_New_Sprite_Block(para);

	if(node != NULL)
	{
		list_add_node_tail(node,&gsprite.header);
		return node;
	}
	return NULL;
}

//在链表中寻找block id,并返回该结点的指针
static list_head_t *  List_Find_Sprite_Block(__s32 id)
{
	list_head_t * guard = NULL;

	guard = gsprite.header;

	if(guard != NULL)
	{
		do
		{
			if(guard->data->id == id)
			{
				return guard;
			}
			guard = guard->next;
		}
		while(guard != gsprite.header);
	}

	return NULL;

}

//从链表中删除block id,并返回该block的指针
static list_head_t * List_Delete_Sprite_Block(list_head_t * node)
{
	__s32 id = 0;

	if(node != NULL)
	{
	    id = node->data->id;
		if(id == 0)//delete the first block
		{
			__s32 next_id = 0;
			list_head_t * next_node = NULL;

			next_id = node->next->data->id;
			next_node = node->next;

			if(id == next_id)//free the only block
			{
				gsprite.header = NULL;
			}
			else
			{
				__s32 id_tmp = 0;

				id_tmp = gsprite.sprite_hid[0];
				gsprite.sprite_hid[0] = gsprite.sprite_hid[next_id];
				gsprite.sprite_hid[next_id] = id_tmp;

				next_node->data->id = 0;
				node->data->id = next_id;

				gsprite.header = next_node;
			}
		}
		list_del_node(node);
		return node;
	}
	else
	{
		return NULL;
	}
}

//从链表中删除block id,并释放其空间,返回该block的id(该id可能不是其原来的id)
static __s32 List_Delete_Free_Sprite_Block(list_head_t * node)
{
    __s32 ret = -1;

	if(node != NULL)
	{
	    List_Delete_Sprite_Block(node);
	    ret = node->data->id;
		list_free_node(node);
	}
	return ret;
}

static __s32 List_Assert_Sprite_Block(list_head_t * dst_node, list_head_t * node)
{
	list_head_t * next_node = NULL;

	if(gsprite.header == NULL)
	{
		gsprite.header = node;
		return DIS_SUCCESS;
	}
	else if(dst_node == NULL)//asset to the front of the list
	{
	    __s32 id = 0;
		__s32 id_tmp = 0;

		next_node = gsprite.header;

		id = node->data->id;
		node->data->id = 0;
		next_node->data->id = id;

		id_tmp = gsprite.sprite_hid[0];
		gsprite.sprite_hid[0] = gsprite.sprite_hid[id];
		gsprite.sprite_hid[id] = id_tmp;

		gsprite.header = node;

		dst_node = next_node->prev;
	}
	else
	{
		next_node = dst_node->next;
	}
	___list_add(node,dst_node,next_node);

	return DIS_SUCCESS;
}

static __s32 List_Get_First_Sprite_Block_Id(void)
{
	if(gsprite.header == NULL)
	{
		return -1;
	}
	else
	{
		return gsprite.header->data->id;
	}
}

static __s32 List_Get_Last_Sprite_Block_Id(void)
{
	if(gsprite.header == NULL)
	{
		return -1;
	}
	else
	{
		return gsprite.header->prev->data->id;
	}
}

static __s32 sprite_set_sprite_block_para(__u32 id, __u32 next_id, __disp_sprite_block_para_t * para)
{
    __u32 bpp, addr;

    bpp = de_format_to_bpp(gsprite.format);

	addr = DE_BE_Offset_To_Addr((__u32)para->fb.addr[0] ,para->fb.size.width, para->src_win.x, para->src_win.y, bpp);
	DE_BE_Sprite_Block_Set_fb(id, (__u32)OSAL_VAtoPA((void*)addr), para->fb.size.width*(bpp>>3));
	DE_BE_Sprite_Block_Set_Pos(id, para->scn_win.x, para->scn_win.y);
	DE_BE_Sprite_Block_Set_Size(id, para->scn_win.width, para->scn_win.height);
	DE_BE_Sprite_Block_Set_Next_Id(id, next_id);

    OSAL_CacheRangeFlush((void*)para->fb.addr[0], (para->fb.size.width * para->scn_win.height * bpp + 7)/8,CACHE_CLEAN_FLUSH_D_CACHE_REGION);

    return 0;
}

__s32 BSP_disp_sprite_init(__u32 sel)
{
	__s32 i = 0;

	memset(&gsprite,0,sizeof(sprite_t));
	gsprite.status = 0;
	for(i = 0;i<MAX_SPRITE_BLOCKS;i++)
    {
        gsprite.block_status[i] = 0;
        gsprite.sprite_hid[i] = 100+i;
    }

	return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_exit(__u32 sel)
{
	__s32 i = 0;
	list_head_t * pGuard = NULL;
	list_head_t * pNext = NULL;

	gsprite.status = 0;
	for(i = 0;i<MAX_SPRITE_BLOCKS;i++)
	{
		gsprite.block_status[i] = 0;
		gsprite.sprite_hid[i] = 100+i;
	}

	pGuard = gsprite.header;
	pGuard->prev->next = NULL;
	while(pGuard != NULL)
	{
		pNext = pGuard->next;
		list_free_node(pGuard);
		pGuard = pNext;
	}

	return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_open(__u32 sel)
{
    __u32 cpu_sr;

	if(!gsprite.status & SPRITE_OPENED)
	{
		DE_BE_Sprite_Enable(TRUE);

		OSAL_IrqLock(&cpu_sr);
		gsprite.enable = TRUE;
		gsprite.status|= SPRITE_OPENED;
		OSAL_IrqUnLock(cpu_sr);
	}
	return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_close(__u32 sel)
{
    __u32 cpu_sr;

	if(gsprite.status & SPRITE_OPENED)
	{
		DE_BE_Sprite_Enable(FALSE);

		OSAL_IrqLock(&cpu_sr);
		gsprite.enable = FALSE;
		gsprite.status &=SPRITE_OPENED_MASK;
		OSAL_IrqUnLock(cpu_sr);
	}
	return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_alpha_enable(__u32 sel)
{
	DE_BE_Sprite_Global_Alpha_Enable(TRUE);
	gsprite.global_alpha_enable = TRUE;

	return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_alpha_disable(__u32 sel)
{
	DE_BE_Sprite_Global_Alpha_Enable(FALSE);
	gsprite.global_alpha_enable = FALSE;

	return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_get_alpha_enable(__u32 sel)
{
	return gsprite.global_alpha_enable;
}

__s32 BSP_disp_sprite_set_alpha_vale(__u32 sel, __u32 alpha)
{
	DE_BE_Sprite_Set_Global_Alpha(alpha);
	gsprite.global_alpha_value = alpha;

	return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_get_alpha_value(__u32 sel)
{
	return gsprite.global_alpha_value;
}

__s32 BSP_disp_sprite_set_format(__u32 sel, __disp_pixel_fmt_t format, __disp_pixel_seq_t pixel_seq)
{
	gsprite.format = format;
	gsprite.pixel_seq = pixel_seq;
	DE_BE_Sprite_Set_Format(pixel_seq,(format==DISP_FORMAT_ARGB8888)?0:1);

	return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_set_palette_table(__u32 sel, __u32 *buffer, __u32 offset, __u32 size)
{
    if((buffer == NULL) || (size < 0) || (offset < 0) || ((offset+size)>1024))
    {
        DE_WRN("para invalid in BSP_disp_sprite_set_palette_table\n");
        return DIS_PARA_FAILED;
    }

    DE_BE_Sprite_Set_Palette_Table((__u32)buffer,offset,size);

    return DIS_SUCCESS;
}

__s32 BSP_disp_sprite_set_order(__u32 sel, __s32 hid,__s32 dst_hid)//todo
{
	__s32 id = 0, dst_id = 0;
	list_head_t * node = NULL, * dst_node = NULL, *chg_node0 = NULL, *chg_node1 = NULL;
	__disp_sprite_block_para_t para;

	id = Sprite_Hid_To_Id(hid);
	dst_id = Sprite_Hid_To_Id(dst_hid);
	if((gsprite.block_status[id] & SPRITE_BLOCK_USED)
		&& (dst_id == -1 || (gsprite.block_status[dst_id] & SPRITE_BLOCK_USED)))
	{
		if(id == dst_id)//same block,not need to move
		{
			return DIS_SUCCESS;
		}
		if(dst_id != -1)
		{
			dst_node = List_Find_Sprite_Block(dst_id);
			if(dst_node->next->data->id == id && id != 0)//it is the order,not need to move
			{
				return DIS_SUCCESS;
			}
		}
		else
		{
		    dst_node = NULL;
		}

		node = List_Find_Sprite_Block(id);
		if(id == 0)//the block is the first block
		{
			chg_node0 = node->next;
		}
		else
		{
			chg_node0 = node->prev;
		}

		if(dst_id == -1)//move to the front of the list
		{
			chg_node1 = gsprite.header;
		}
		else
		{
			chg_node1 = List_Find_Sprite_Block(dst_id);
		}

		List_Delete_Sprite_Block(node);
		List_Assert_Sprite_Block(dst_node,node);

		para.fb.addr[0] = node->data->address;
		para.fb.size.width = node->data->size.width;
		para.src_win.x = node->data->src_win.x;
		para.src_win.y = node->data->src_win.y;
		memcpy(&para.scn_win,&node->data->scn_win,sizeof(__disp_rect_t));
		if(node->data->enable == FALSE)
		{
			para.scn_win.y = -2000;
		}
		sprite_set_sprite_block_para(node->data->id,node->next->data->id,&para);

		para.fb.addr[0] = chg_node0->data->address;
		para.fb.size.width = chg_node0->data->size.width;
		para.src_win.x = chg_node0->data->src_win.x;
		para.src_win.y = chg_node0->data->src_win.y;
		memcpy(&para.scn_win,&chg_node0->data->scn_win,sizeof(__disp_rect_t));
		if(chg_node0->data->enable == FALSE)
		{
			para.scn_win.y = -2000;
		}
		sprite_set_sprite_block_para(chg_node0->data->id,chg_node0->next->data->id,&para);

		para.fb.addr[0] = chg_node1->data->address;
		para.fb.size.width = chg_node1->data->size.width;
		para.src_win.x = chg_node1->data->src_win.x;
		para.src_win.y = chg_node1->data->src_win.y;
		memcpy(&para.scn_win,&chg_node1->data->scn_win,sizeof(__disp_rect_t));
		if(chg_node1->data->enable == FALSE)
		{
			para.scn_win.y = -2000;
		}
		sprite_set_sprite_block_para(chg_node1->data->id,chg_node1->next->data->id,&para);

		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_sprite_get_top_block(__u32 sel)
{
	__u32 id;

	id = List_Get_First_Sprite_Block_Id();
	return Sprite_Id_To_Hid(id);
}

__s32 BSP_disp_sprite_get_bottom_block(__u32 sel)
{
	__u32 id;

	id = List_Get_Last_Sprite_Block_Id();
	return Sprite_Id_To_Hid(id);
}

__s32 BSP_disp_sprite_get_block_number(__u32 sel)
{
	return gsprite.block_num;
}

//the para including fb address,fb width,fb height,source x/y offset,screen window
__s32 BSP_disp_sprite_block_request(__u32 sel, __disp_sprite_block_para_t *para)
{
	__s32 id = 0;
	__disp_sprite_block_para_t cur_para;
	list_head_t * node = NULL;
	__u32 cpu_sr;

	if((para->scn_win.width != 8) && (para->scn_win.width != 16) && (para->scn_win.width != 32)
		&& (para->scn_win.width != 64) && (para->scn_win.width != 128) && (para->scn_win.width != 256)
		&& (para->scn_win.width != 512))
	{
		DE_WRN("BSP_disp_sprite_block_request,scn_win width invalid:%d\n",para->scn_win.width);
		return 0;
	}
	if((para->scn_win.height != 8) && (para->scn_win.height != 16) && (para->scn_win.height != 32)
		&& (para->scn_win.height != 64) && (para->scn_win.height != 128) && (para->scn_win.height != 256)
		&& (para->scn_win.height != 512) && (para->scn_win.height != 1024))
	{
		DE_WRN("BSP_disp_sprite_block_request,scn_win height invalid:%d\n",para->scn_win.height);
		return 0;
	}

    node = List_Add_Sprite_Block(para);
    if(node == NULL)
    {
        return (__s32)NULL;
    }

    id = node->data->id;
	node->data->address = (__u32)para->fb.addr[0];
	node->data->size.width = para->fb.size.width;
	node->data->src_win.x = para->src_win.x;
	node->data->src_win.y = para->src_win.y;
	node->data->scn_win.x = para->scn_win.x;
	node->data->scn_win.y = para->scn_win.y;
	node->data->scn_win.width = para->scn_win.width;
	node->data->scn_win.height = para->scn_win.height;

    memcpy(&cur_para,para,sizeof(__disp_sprite_block_para_t));
    cur_para.scn_win.y = -2000;

	DE_BE_Sprite_Block_Set_Next_Id(node->prev->data->id, id);
	sprite_set_sprite_block_para(id, 0, para);

    OSAL_IrqLock(&cpu_sr);
	gsprite.block_status[id] |= SPRITE_BLOCK_USED;
	gsprite.block_num ++;
    OSAL_IrqUnLock(cpu_sr);

    return Sprite_Id_To_Hid(id);

}

__s32 BSP_disp_sprite_block_release(__u32 sel, __s32 hid)
{
	__s32 id = 0,pre_id = 0,next_id = 0;
	list_head_t * node = NULL, *next_node=NULL, *pre_node=NULL;
	__s32 release_id = 0;
	__u32 cpu_sr;

	id = Sprite_Hid_To_Id(hid);

	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);
		pre_node = node->prev;
		next_node = node->next;
		pre_id = node->prev->data->id;
		next_id = node->next->data->id;
		release_id = List_Delete_Free_Sprite_Block(node);

		if(id == pre_id)//release the only block
		{
			__disp_sprite_block_para_t para;

			para.fb.addr[0] = 0;
			para.fb.size.width = 8;
			para.fb.format = DISP_FORMAT_ARGB8888;
			para.src_win.x = 0;
			para.src_win.y = 0;
			para.scn_win.x = 0;
			para.scn_win.y = -2000;
			para.scn_win.width = 8;
			para.scn_win.height = 8;

			sprite_set_sprite_block_para(id,0,&para);
		}
		else if(id == 0)//release the first block
		{
			__disp_sprite_block_para_t para;

			para.fb.addr[0] = next_node->data->address;
			para.fb.size.width = next_node->data->size.width;
			para.src_win.x = next_node->data->src_win.x;
			para.src_win.y = next_node->data->src_win.y;
			para.scn_win.x = next_node->data->scn_win.x;
			if(next_node->data->enable == FALSE)
            {
                para.scn_win.y = -2000;
            }
            else
            {
				para.scn_win.y = next_node->data->scn_win.y;
			}
			para.scn_win.width = next_node->data->scn_win.width;
			para.scn_win.height = next_node->data->scn_win.height;
			sprite_set_sprite_block_para(0,next_node->next->data->id,&para);

			para.fb.addr[0] = 0;
			para.fb.size.width= 8;
			para.src_win.x = 0;
			para.src_win.y = 0;
			para.scn_win.x = 0;
			para.scn_win.y = -2000;
			para.scn_win.width = 8;
			para.scn_win.height = 8;
			sprite_set_sprite_block_para(next_id,0,&para);
		}
		else
		{
			__disp_sprite_block_para_t para;

			para.fb.addr[0] = pre_node->data->address;
			para.fb.size.width= pre_node->data->size.width;
			para.src_win.x = pre_node->data->src_win.x;
			para.src_win.y = pre_node->data->src_win.y;
			para.scn_win.x = pre_node->data->scn_win.x;
            if(node->data->enable == FALSE)
            {
                para.scn_win.y = -2000;
            }
            else
            {
			    para.scn_win.y = pre_node->data->scn_win.y;
            }
			para.scn_win.width = pre_node->data->scn_win.width;
			para.scn_win.height = pre_node->data->scn_win.height;
			sprite_set_sprite_block_para(pre_id,next_id,&para);

			para.fb.addr[0] = 0;
			para.fb.size.width = 8;
			para.src_win.x = 0;
			para.src_win.y = 0;
			para.scn_win.x = 0;
			para.scn_win.y = -2000;
			para.scn_win.width = 8;
			para.scn_win.height = 8;
			sprite_set_sprite_block_para(id,0,&para);
		}

		OSAL_IrqLock(&cpu_sr);
		gsprite.block_status[release_id] &= SPRITE_BLOCK_USED_MASK;
		gsprite.block_num --;
		OSAL_IrqUnLock(cpu_sr);

		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

//setting srceen window(x,y,width,height)
__s32 BSP_disp_sprite_block_set_screen_win(__u32 sel, __s32 hid, __disp_rect_t * scn_win)
{
	__s32 id = 0;
	list_head_t * node = NULL;
	__disp_rect_t cur_scn;
	__u32 cpu_sr;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		if((scn_win->width != 8) && (scn_win->width != 16) && (scn_win->width != 32)
			&& (scn_win->width != 64) && (scn_win->width != 128) && (scn_win->width != 256)
			&& (scn_win->width != 512))
		{
			DE_WRN("BSP_disp_sprite_block_set_screen_win,scn_win width invalid:%d\n",scn_win->width);
			return DIS_PARA_FAILED;
		}
		if((scn_win->height != 8) && (scn_win->height != 16) && (scn_win->height != 32)
			&& (scn_win->height != 64) && (scn_win->height != 128) && (scn_win->height != 256)
			&& (scn_win->height != 512) && (scn_win->height != 1024))
		{
			DE_WRN("BSP_disp_sprite_block_set_screen_win,scn_win height invalid:%d\n",scn_win->height);
			return DIS_PARA_FAILED;
		}

		node = List_Find_Sprite_Block(id);
		if(node == NULL)
		{
			return DIS_PARA_FAILED;
		}

		cur_scn.x = scn_win->x;
		cur_scn.y = scn_win->y;
		cur_scn.width = scn_win->width;
		cur_scn.height = scn_win->height;

		if(node->data->enable == FALSE)
		{
			cur_scn.y = -2000;
		}
    	DE_BE_Sprite_Block_Set_Pos(id,cur_scn.x,cur_scn.y);
    	DE_BE_Sprite_Block_Set_Size(id,cur_scn.width,cur_scn.height);

		OSAL_IrqLock(&cpu_sr);
		node->data->scn_win.x = scn_win->x;
		node->data->scn_win.y = scn_win->y;
		node->data->scn_win.width = scn_win->width;
		node->data->scn_win.height = scn_win->height;
		OSAL_IrqUnLock(cpu_sr);
		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}

}

__s32 BSP_disp_sprite_block_get_srceen_win(__u32 sel, __s32 hid, __disp_rect_t * scn_win)
{
	__s32 id = 0;
	list_head_t * node = NULL;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);

		scn_win->x = node->data->scn_win.x;
		scn_win->y = node->data->scn_win.y;
		scn_win->width = node->data->scn_win.width;
		scn_win->height = node->data->scn_win.height;

		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

//setting source x/y offset
__s32 BSP_disp_sprite_block_set_src_win(__u32 sel, __s32 hid, __disp_rect_t * src_win)
{
	__s32 id = 0;
	list_head_t * node = NULL;
	__u32 cpu_sr;
	__u32 bpp, addr;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
        node = List_Find_Sprite_Block(id);

        bpp = de_format_to_bpp(gsprite.format);
        addr = DE_BE_Offset_To_Addr(node->data->address, node->data->size.width, src_win->x, src_win->y, bpp);
        DE_BE_Sprite_Block_Set_fb(id,(__u32)OSAL_VAtoPA((void*)addr),node->data->size.width*(bpp>>3));

        OSAL_IrqLock(&cpu_sr);
        node->data->src_win.x = src_win->x;
        node->data->src_win.y = src_win->y;
        OSAL_IrqUnLock(cpu_sr);

        return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}

}

__s32 BSP_disp_sprite_block_get_src_win(__u32 sel, __s32 hid, __disp_rect_t * src_win)
{
	__s32 id = 0;
	list_head_t * node = NULL;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);

		src_win->x = node->data->src_win.x;
		src_win->y = node->data->src_win.y;
		src_win->width = node->data->scn_win.width;
		src_win->height = node->data->scn_win.height;

		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

//setting fb address,fb width,fb height;keep the source x/y offset
__s32 BSP_disp_sprite_block_set_framebuffer(__u32 sel, __s32 hid, __disp_fb_t * fb)
{
	__s32 id = 0;
	list_head_t * node = NULL;
	__s32 bpp = 0, addr;
	__u32 cpu_sr;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);

		bpp = de_format_to_bpp(gsprite.format);
		OSAL_CacheRangeFlush((void *)fb->addr[0], (fb->size.width * node->data->src_win.height * bpp + 7)/8,CACHE_CLEAN_FLUSH_D_CACHE_REGION);

    	addr = DE_BE_Offset_To_Addr(fb->addr[0], fb->size.width, node->data->src_win.x, node->data->src_win.y, bpp);
        DE_BE_Sprite_Block_Set_fb(id,(__u32)OSAL_VAtoPA((void*)addr), fb->size.width*(bpp>>3));

		OSAL_IrqLock(&cpu_sr);
		node->data->address = fb->addr[0];
		node->data->size.width = fb->size.width;
		node->data->size.height = fb->size.height;
		OSAL_IrqUnLock(cpu_sr);

		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}

}

__s32 BSP_disp_sprite_block_get_framebufer(__u32 sel, __s32 hid,__disp_fb_t *fb)
{
	__s32 id = 0;
	list_head_t * node = NULL;

	id = Sprite_Hid_To_Id(hid);

	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);

		fb->format = gsprite.format;
		fb->seq = gsprite.pixel_seq;
		fb->addr[0] = node->data->address;
		fb->size.width = node->data->size.width;
		fb->size.height = node->data->size.height;
		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

//setting fb address,fb width,fb height,source x/y offset,screen window
__s32 BSP_disp_sprite_block_set_para(__u32 sel, __u32 hid,__disp_sprite_block_para_t *para)
{
	__s32 id = 0;
	list_head_t * node = NULL;
	__disp_sprite_block_para_t cur_para;
	__u32 cpu_sr;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);

		memcpy(&cur_para,para,sizeof(__disp_sprite_block_para_t));
		if(node->data->enable == FALSE)
		{
			cur_para.scn_win.y = -2000;
		}

	    sprite_set_sprite_block_para(id, node->next->data->id, &cur_para);

		OSAL_IrqLock(&cpu_sr);
		node->data->address = para->fb.addr[0];
		node->data->size.width = para->fb.size.width;
		node->data->size.height = para->fb.size.height;
		node->data->src_win.x = para->src_win.x;
		node->data->src_win.y = para->src_win.y;
		node->data->scn_win.x = para->scn_win.x;
		node->data->scn_win.y = para->scn_win.y;
		node->data->scn_win.width = para->scn_win.width;
		node->data->scn_win.height = para->scn_win.height;
		OSAL_IrqUnLock(cpu_sr);
		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_sprite_block_get_para(__u32 sel, __u32 hid,__disp_sprite_block_para_t *para)
{
	__s32 id = 0;
	list_head_t * node = NULL;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);

		para->fb.format = gsprite.format;
		para->fb.addr[0] = node->data->address;
		para->fb.size.width = node->data->size.width;
		para->fb.size.height = node->data->size.height;
		para->src_win.x = node->data->src_win.x;
		para->src_win.y = node->data->src_win.y;
		para->src_win.width = node->data->scn_win.width;
		para->src_win.height = node->data->scn_win.height;
		para->scn_win.x = node->data->scn_win.x;
		para->scn_win.y = node->data->scn_win.y;
		para->scn_win.width = node->data->scn_win.width;
		para->scn_win.height = node->data->scn_win.height;

		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_sprite_block_set_top(__u32 sel, __u32 hid)
{
	__u32 id;

	id = List_Get_Last_Sprite_Block_Id();
	return BSP_disp_sprite_set_order(sel, hid,Sprite_Id_To_Hid(id));
}

__s32 BSP_disp_sprite_block_set_bottom(__u32 sel, __u32 hid)
{
	return BSP_disp_sprite_set_order(sel, hid,0);
}

__s32 BSP_disp_sprite_block_get_pre_block(__u32 sel, __u32 hid)
{
	__s32 id = 0;
	list_head_t * node = NULL;

	id = Sprite_Hid_To_Id(hid);

	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);
		if(node == gsprite.header)//the block is the first
		{
			return 0;
		}
		return Sprite_Id_To_Hid(node->prev->data->id);
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_sprite_block_get_next_block(__u32 sel, __u32 hid)
{
	__s32 id = 0;
	list_head_t * node = NULL;

	id = Sprite_Hid_To_Id(hid);

	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);
		if(node == gsprite.header->prev)//the block is the last
		{
			return 0;
		}
		return Sprite_Id_To_Hid(node->next->data->id);
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_sprite_block_get_prio(__u32 sel, __u32 hid)
{
	__s32 id = 0;
	__s32 prio = 0;

	id = Sprite_Hid_To_Id(hid);

	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		list_head_t * guard = NULL;
		guard = gsprite.header;
		if(guard != NULL)
		{
			do
			{
				if(guard->data->id == id)
				{
					return prio;
				}
				guard = guard->next;
				prio ++;
			}
			while(guard != gsprite.header);
		}
		return DIS_FAIL;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_sprite_block_open(__u32 sel, __u32 hid)
{
	__s32 id = 0;
	list_head_t * node = NULL;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);
		if(node->data->enable == FALSE)
		{
        	DE_BE_Sprite_Block_Set_Pos(id,node->data->scn_win.x,node->data->scn_win.y);
        	DE_BE_Sprite_Block_Set_Size(id,node->data->scn_win.width,node->data->scn_win.height);
			node->data->enable = TRUE;
		}
		gsprite.block_status[id] |= SPRITE_BLOCK_OPENED;
		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_sprite_block_close(__u32 sel, __u32 hid)
{
	__s32 id = 0;
	list_head_t * node = NULL;
	__disp_rect_t scn_win;

	id = Sprite_Hid_To_Id(hid);
	if(gsprite.block_status[id] & SPRITE_BLOCK_USED)
	{
		node = List_Find_Sprite_Block(id);
		if(node->data->enable == TRUE)
		{
			scn_win.x = 0;
			scn_win.y = -2000;
			scn_win.width = node->data->scn_win.width;
			scn_win.height = node->data->scn_win.height;
        	DE_BE_Sprite_Block_Set_Pos(id,scn_win.x,scn_win.y);
        	DE_BE_Sprite_Block_Set_Size(id,scn_win.width,scn_win.height);
			node->data->enable = FALSE;
		}
		gsprite.block_status[id] &= SPRITE_BLOCK_OPEN_MASK;
		return DIS_SUCCESS;
	}
	else
	{
		return DIS_OBJ_NOT_INITED;
	}
}

