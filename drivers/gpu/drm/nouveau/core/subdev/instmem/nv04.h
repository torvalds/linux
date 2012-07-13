#ifndef __NV04_INSTMEM_H__
#define __NV04_INSTMEM_H__

struct nv04_instmem_priv {
	struct nouveau_gpuobj *vbios;
	struct nouveau_gpuobj *ramht;
	struct nouveau_gpuobj *ramro;
	struct nouveau_gpuobj *ramfc;
};

#endif
