#ifndef __NVKM_I2C_PORT_H__
#define __NVKM_I2C_PORT_H__
#include "priv.h"

#ifndef MSG
#define MSG(l,f,a...) do {                                                     \
	struct nvkm_i2c_port *_port = (void *)port;                         \
	nv_##l(_port, "PORT:%02x: "f, _port->index, ##a);                      \
} while(0)
#define DBG(f,a...) MSG(debug, f, ##a)
#define ERR(f,a...) MSG(error, f, ##a)
#endif
#endif
