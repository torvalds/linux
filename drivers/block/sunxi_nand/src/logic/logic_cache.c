/*
 * drivers/block/sunxi_nand/src/logic/logic_cache.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/module.h>
#include "../include/nand_logic.h"

//#define CACHE_DBG

#define NAND_W_CACHE_EN
#define N_NAND_W_CACHE  8


typedef struct
{
	__u8	*data;
	__u32	size;

	__u32	hit_page;
	__u32   secbitmap;

	__u32	access_count;
}__nand_cache_t;

__u32 g_w_access_cnt;

__nand_cache_t nand_w_cache[N_NAND_W_CACHE];
__nand_cache_t nand_r_cache;

__u32 _get_valid_bits(__u32 secbitmap)
{
	__u32 validbit = 0;

	while(secbitmap)
	{
		if(secbitmap & 0x1)
			validbit++;
		secbitmap >>= 1;
	}

	return validbit;
}

__u32 _get_first_valid_bit(__u32 secbitmap)
{
	__u32 firstbit = 0;

	while(!(secbitmap & 0x1))
	{
		secbitmap >>= 1;
		firstbit++;
	}

	return firstbit;
}

__s32 _flush_w_cache(void)
{
	__u32	i;

	for(i = 0; i < N_NAND_W_CACHE; i++)
	{
		if(nand_w_cache[i].hit_page != 0xffffffff)
		{
			if(nand_w_cache[i].secbitmap != FULL_BITMAP_OF_LOGIC_PAGE)
				LML_PageRead(nand_w_cache[i].hit_page,(nand_w_cache[i].secbitmap ^ FULL_BITMAP_OF_LOGIC_PAGE)&FULL_BITMAP_OF_LOGIC_PAGE,nand_w_cache[i].data);

			LML_PageWrite(nand_w_cache[i].hit_page,FULL_BITMAP_OF_LOGIC_PAGE,nand_w_cache[i].data);
			nand_w_cache[i].hit_page = 0xffffffff;
			nand_w_cache[i].secbitmap = 0;
			nand_w_cache[i].access_count = 0;

			/*disable read cache with current page*/
			if (nand_r_cache.hit_page == nand_w_cache[i].hit_page){
					nand_r_cache.hit_page = 0xffffffff;
					nand_r_cache.secbitmap = 0;
			}

		}
	}


	return 0;

}

__s32 _flush_w_cache_simple(__u32 i)
{
	if(nand_w_cache[i].hit_page != 0xffffffff)
	{
		if(nand_w_cache[i].secbitmap != FULL_BITMAP_OF_LOGIC_PAGE)
			LML_PageRead(nand_w_cache[i].hit_page,(nand_w_cache[i].secbitmap ^ FULL_BITMAP_OF_LOGIC_PAGE)&FULL_BITMAP_OF_LOGIC_PAGE,nand_w_cache[i].data);

		LML_PageWrite(nand_w_cache[i].hit_page,FULL_BITMAP_OF_LOGIC_PAGE,nand_w_cache[i].data);
		nand_w_cache[i].hit_page = 0xffffffff;
		nand_w_cache[i].secbitmap = 0;
		nand_w_cache[i].access_count = 0;

		/*disable read cache with current page*/
		if (nand_r_cache.hit_page == nand_w_cache[i].hit_page){
				nand_r_cache.hit_page = 0xffffffff;
				nand_r_cache.secbitmap = 0;
		}

	}

	return 0;

}


__s32 NAND_CacheFlush(void)
{
	//__u32	i;

	_flush_w_cache();

	return 0;

}

void _get_data_from_cache(__u32 blk, __u32 nblk, void *buf)
{
	__u32 i;
	__u32 sec;
	__u32 page,SecBitmap,SecWithinPage;

	for(sec = blk; sec < blk + nblk; sec++)
	{
		SecWithinPage = sec % SECTOR_CNT_OF_LOGIC_PAGE;
		SecBitmap = (1 << SecWithinPage);
		page = sec / SECTOR_CNT_OF_LOGIC_PAGE;
		for (i = 0; i < N_NAND_W_CACHE; i++)
		{
			if ((nand_w_cache[i].hit_page == page) && (nand_w_cache[i].secbitmap & SecBitmap))
			{
				MEMCPY((__u8 *)buf + (sec - blk) * 512, nand_w_cache[i].data + SecWithinPage * 512,512);
				break;
			}
		}
	}
}

void _get_one_page(__u32 page,__u32 SecBitmap,__u8 *data)
{
	__u32 i;
	__u8 *tmp = data;


	if(page == nand_r_cache.hit_page)
	{
		for(i = 0;i < SECTOR_CNT_OF_LOGIC_PAGE; i++)
		{
			if(SecBitmap & (1<<i))
			{
				MEMCPY(tmp + (i<<9),nand_r_cache.data + (i<<9),512);
			}
		}
	}

	else
	{
		if(SecBitmap == FULL_BITMAP_OF_LOGIC_PAGE)
		{
			LML_PageRead(page,FULL_BITMAP_OF_LOGIC_PAGE,tmp);
		}
		else
		{
			LML_PageRead(page,FULL_BITMAP_OF_LOGIC_PAGE,nand_r_cache.data);
			nand_r_cache.hit_page = page;
			nand_r_cache.secbitmap = FULL_BITMAP_OF_LOGIC_PAGE;

			for(i = 0;i < SECTOR_CNT_OF_LOGIC_PAGE; i++)
			{
				if(SecBitmap & (1<<i))
				{
					MEMCPY(tmp + (i<<9),nand_r_cache.data + (i<<9),512);
				}
			}
		}
	}

	SecBitmap = 0;
}

__s32 NAND_CacheRead(__u32 blk, __u32 nblk, void *buf)
{
	__u32	nSector,StartSec;
	__u32	page;
	__u32	SecBitmap,SecWithinPage;
	__u8 	*pdata;

	nSector 	= nblk;
	StartSec 	= blk;
	SecBitmap 	= 0;
	page 		= 0xffffffff;
	pdata		= (__u8 *)buf;

	/*combind sectors to pages*/
	while(nSector)
	{
		SecWithinPage = StartSec % SECTOR_CNT_OF_LOGIC_PAGE;
		SecBitmap |= (1 << SecWithinPage);
		page = StartSec / SECTOR_CNT_OF_LOGIC_PAGE;

		/*close page if last sector*/
		if (SecWithinPage == (SECTOR_CNT_OF_LOGIC_PAGE - 1))
		{

			__u8 *tmp = pdata + 512 - 512*_get_valid_bits(SecBitmap) - 512 * _get_first_valid_bit(SecBitmap);
			_get_one_page(page, SecBitmap, tmp);
			SecBitmap = 0;
		}

		/*reset variable*/
		nSector--;
		StartSec++;
		pdata += 512;
	}

	/*fill opened page*/
	if (SecBitmap)
	{
		__u8	*tmp = pdata - 512*_get_valid_bits(SecBitmap) - 512 * _get_first_valid_bit(SecBitmap);
		_get_one_page(page, SecBitmap, tmp);
	}

	/*renew data from cache*/
	_get_data_from_cache(blk,nblk,buf);

	return 0;

}

__s32 _fill_nand_cache(__u32 page, __u32 secbitmap, __u8 *pdata)
{
	__u8	hit;
	__u8	i;
	__u8 	pos = 0xff;

	g_w_access_cnt++;

	hit = 0;

	for (i = 0; i < N_NAND_W_CACHE; i++)
	{
		/*merge data if cache hit*/
		if (nand_w_cache[i].hit_page == page){
			hit = 1;
			MEMCPY(nand_w_cache[i].data + 512 * _get_first_valid_bit(secbitmap),pdata, 512 * _get_valid_bits(secbitmap));
			nand_w_cache[i].secbitmap |= secbitmap;
			nand_w_cache[i].access_count = g_w_access_cnt;
			pos = i;
			break;
		}
	}

	/*post data if cache miss*/
	if (!hit)
	{
		/*find cache to post*/
		for (i = 0; i < N_NAND_W_CACHE; i++)
		{
			if (nand_w_cache[i].hit_page == 0xffffffff)
			{
				pos = i;
				break;
			}
		}

		if (pos == 0xff)
		{
			__u32 access_cnt = nand_w_cache[0].access_count;
			pos = 0;

			for (i = 1; i < N_NAND_W_CACHE; i++)
			{
				if (access_cnt > nand_w_cache[i].access_count)
				{
					pos = i;
					access_cnt = nand_w_cache[i].access_count;
				}

				if((nand_w_cache[i].hit_page == page-1)&&(page>0))
				{
				    pos = i;
				    break;
				}
			}

			if(nand_w_cache[pos].secbitmap != FULL_BITMAP_OF_LOGIC_PAGE)
				LML_PageRead(nand_w_cache[pos].hit_page,nand_w_cache[pos].secbitmap ^ FULL_BITMAP_OF_LOGIC_PAGE,nand_w_cache[pos].data);

			LML_PageWrite(nand_w_cache[pos].hit_page, FULL_BITMAP_OF_LOGIC_PAGE, nand_w_cache[pos].data);
			nand_w_cache[pos].access_count = 0;

			//add by penggang
			/*disable read cache with current page*/
			if (nand_r_cache.hit_page == nand_w_cache[pos].hit_page){
				nand_r_cache.hit_page = 0xffffffff;
				nand_r_cache.secbitmap = 0;
			}

		}

		/*merge data*/
		MEMCPY(nand_w_cache[pos].data + 512 * _get_first_valid_bit(secbitmap),pdata, 512 * _get_valid_bits(secbitmap));
		nand_w_cache[pos].hit_page = page;
		nand_w_cache[pos].secbitmap = secbitmap;
		nand_w_cache[pos].access_count = g_w_access_cnt;

	}

	if (g_w_access_cnt == 0)
	{
		for (i = 0; i < N_NAND_W_CACHE; i++)
			nand_w_cache[i].access_count = 0;
		g_w_access_cnt = 1;
		nand_w_cache[pos].access_count = g_w_access_cnt;
	}

	return 0;
}

__s32 NAND_CacheWrite(__u32 blk, __u32 nblk, void *buf)
{
	__u32	nSector,StartSec;
	__u32	page;
	__u32	SecBitmap,SecWithinPage;
	__u32	i;
	__u8 	*pdata;
	//__u32  hit = 0;

	nSector 	= nblk;
	StartSec 	= blk;
	SecBitmap 	= 0;
	page 		= 0xffffffff;
	pdata		= (__u8 *)buf;

	/*combind sectors to pages*/
	while(nSector)
	{
		SecWithinPage = StartSec % SECTOR_CNT_OF_LOGIC_PAGE;
		SecBitmap |= (1 << SecWithinPage);
		page = StartSec / SECTOR_CNT_OF_LOGIC_PAGE;


		/*close page if last sector*/
		if (SecWithinPage == (SECTOR_CNT_OF_LOGIC_PAGE - 1))
		{
			/*write to nand flash if align one logic page*/
			if(SecBitmap == FULL_BITMAP_OF_LOGIC_PAGE)
			{
				/*disable write cache with current page*/
				for (i = 0; i < N_NAND_W_CACHE; i++)
				{
					if(nand_w_cache[i].hit_page == page)
					{
						nand_w_cache[i].hit_page = 0xffffffff;
						nand_w_cache[i].secbitmap = 0;
						//hit=1;
					}
					else if((nand_w_cache[i].hit_page == page-1)&&(page>0))
					{
					    _flush_w_cache_simple(i);
					}


				}
				/*disable read cache with current page*/
				if (nand_r_cache.hit_page == page){
					nand_r_cache.hit_page = 0xffffffff;
					nand_r_cache.secbitmap = 0;
				}

                //if(!hit)
                //    _flush_w_cache();

				LML_PageWrite(page,FULL_BITMAP_OF_LOGIC_PAGE,pdata + 512 - 512*SECTOR_CNT_OF_LOGIC_PAGE);
			}

			/*fill to cache if unalign one logic page*/
			else
				_fill_nand_cache(page, SecBitmap, pdata + 512 - 512*_get_valid_bits(SecBitmap));

			SecBitmap = 0;
		}


		/*reset variable*/
		nSector--;
		StartSec++;
		pdata += 512;
	}

	/*fill opened page*/
	if (SecBitmap)
		_fill_nand_cache(page,SecBitmap,pdata - 512*_get_valid_bits(SecBitmap));

	return 0;
}


__s32 NAND_CacheOpen(void)
{
	__u32 i;

	g_w_access_cnt = 0;

	for(i = 0; i < N_NAND_W_CACHE; i++)
	{
		nand_w_cache[i].size = 512 * SECTOR_CNT_OF_LOGIC_PAGE;
		nand_w_cache[i].data = MALLOC(nand_w_cache[i].size);
		nand_w_cache[i].hit_page = 0xffffffff;
		nand_w_cache[i].secbitmap = 0;
		nand_w_cache[i].access_count = 0;
	}

	nand_r_cache.size = 512 * SECTOR_CNT_OF_LOGIC_PAGE;
	nand_r_cache.data = MALLOC(nand_r_cache.size);
	nand_r_cache.hit_page = 0xffffffff;
	nand_r_cache.secbitmap = 0;
	nand_r_cache.access_count = 0;

	return 0;
}

__s32 NAND_CacheClose(void)
{
	__u32 i;

	NAND_CacheFlush();

	#ifdef NAND_W_CACHE_EN
		for(i = 0; i < N_NAND_W_CACHE; i++)
			FREE(nand_w_cache[i].data,nand_w_cache[i].size);
	#endif
	FREE(nand_r_cache.data,nand_r_cache.size);
	return 0;
}
