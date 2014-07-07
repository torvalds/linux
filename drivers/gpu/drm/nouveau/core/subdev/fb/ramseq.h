#ifndef __NVKM_FBRAM_SEQ_H__
#define __NVKM_FBRAM_SEQ_H__

#include <subdev/bus.h>
#include <subdev/bus/hwsq.h>

#define ram_init(s,p)       hwsq_init(&(s)->base, (p))
#define ram_exec(s,e)       hwsq_exec(&(s)->base, (e))
#define ram_have(s,r)       ((s)->r_##r.addr != 0x000000)
#define ram_rd32(s,r)       hwsq_rd32(&(s)->base, &(s)->r_##r)
#define ram_wr32(s,r,d)     hwsq_wr32(&(s)->base, &(s)->r_##r, (d))
#define ram_nuke(s,r)       hwsq_nuke(&(s)->base, &(s)->r_##r)
#define ram_mask(s,r,m,d)   hwsq_mask(&(s)->base, &(s)->r_##r, (m), (d))
#define ram_setf(s,f,d)     hwsq_setf(&(s)->base, (f), (d))
#define ram_wait(s,f,d)     hwsq_wait(&(s)->base, (f), (d))
#define ram_nsec(s,n)       hwsq_nsec(&(s)->base, (n))

#endif
