#ifndef __NV31_MPEG_H__
#define __NV31_MPEG_H__
#include <engine/mpeg.h>

struct nv31_mpeg_chan {
	struct nvkm_object base;
};

struct nv31_mpeg_priv {
	struct nvkm_mpeg base;
	struct nv31_mpeg_chan *chan;
};
#endif
