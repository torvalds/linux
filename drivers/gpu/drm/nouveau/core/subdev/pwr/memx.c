#ifndef __NVKM_PWR_MEMX_H__
#define __NVKM_PWR_MEMX_H__

#include <subdev/pwr.h>
#include <subdev/pwr/fuc/os.h>

struct nouveau_memx {
	struct nouveau_pwr *ppwr;
	u32 base;
	u32 size;
	struct {
		u32 mthd;
		u32 size;
		u32 data[64];
	} c;
};

static void
memx_out(struct nouveau_memx *memx)
{
	struct nouveau_pwr *ppwr = memx->ppwr;
	int i;

	if (memx->c.size) {
		nv_wr32(ppwr, 0x10a1c4, (memx->c.size << 16) | memx->c.mthd);
		for (i = 0; i < memx->c.size; i++)
			nv_wr32(ppwr, 0x10a1c4, memx->c.data[i]);
		memx->c.size = 0;
	}
}

static void
memx_cmd(struct nouveau_memx *memx, u32 mthd, u32 size, u32 data[])
{
	if ((memx->c.size + size >= ARRAY_SIZE(memx->c.data)) ||
	    (memx->c.size && memx->c.mthd != mthd))
		memx_out(memx);
	memcpy(&memx->c.data[memx->c.size], data, size * sizeof(data[0]));
	memx->c.size += size;
	memx->c.mthd  = mthd;
}

int
nouveau_memx_init(struct nouveau_pwr *ppwr, struct nouveau_memx **pmemx)
{
	struct nouveau_memx *memx;
	u32 reply[2];
	int ret;

	ret = ppwr->message(ppwr, reply, PROC_MEMX, MEMX_MSG_INFO, 0, 0);
	if (ret)
		return ret;

	memx = *pmemx = kzalloc(sizeof(*memx), GFP_KERNEL);
	if (!memx)
		return -ENOMEM;
	memx->ppwr = ppwr;
	memx->base = reply[0];
	memx->size = reply[1];

	/* acquire data segment access */
	do {
		nv_wr32(ppwr, 0x10a580, 0x00000003);
	} while (nv_rd32(ppwr, 0x10a580) != 0x00000003);
	nv_wr32(ppwr, 0x10a1c0, 0x01000000 | memx->base);
	nv_wr32(ppwr, 0x10a1c4, 0x00010000 | MEMX_ENTER);
	nv_wr32(ppwr, 0x10a1c4, 0x00000000);
	return 0;
}

int
nouveau_memx_fini(struct nouveau_memx **pmemx, bool exec)
{
	struct nouveau_memx *memx = *pmemx;
	struct nouveau_pwr *ppwr = memx->ppwr;
	u32 finish, reply[2];

	/* flush the cache... */
	memx_out(memx);

	/* release data segment access */
	nv_wr32(ppwr, 0x10a1c4, 0x00000000 | MEMX_LEAVE);
	finish = nv_rd32(ppwr, 0x10a1c0) & 0x00ffffff;
	nv_wr32(ppwr, 0x10a580, 0x00000000);

	/* call MEMX process to execute the script, and wait for reply */
	if (exec) {
		ppwr->message(ppwr, reply, PROC_MEMX, MEMX_MSG_EXEC,
				 memx->base, finish);
	}

	kfree(memx);
	return 0;
}

void
nouveau_memx_wr32(struct nouveau_memx *memx, u32 addr, u32 data)
{
	nv_debug(memx->ppwr, "R[%06x] = 0x%08x\n", addr, data);
	memx_cmd(memx, MEMX_WR32, 2, (u32[]){ addr, data });
}

void
nouveau_memx_wait(struct nouveau_memx *memx,
		  u32 addr, u32 mask, u32 data, u32 nsec)
{
	nv_debug(memx->ppwr, "R[%06x] & 0x%08x == 0x%08x, %d us\n",
				addr, mask, data, nsec);
	memx_cmd(memx, MEMX_WAIT, 4, (u32[]){ addr, ~mask, data, nsec });
	memx_out(memx); /* fuc can't handle multiple */
}

void
nouveau_memx_nsec(struct nouveau_memx *memx, u32 nsec)
{
	nv_debug(memx->ppwr, "    DELAY = %d ns\n", nsec);
	memx_cmd(memx, MEMX_DELAY, 1, (u32[]){ nsec });
	memx_out(memx); /* fuc can't handle multiple */
}

#endif
