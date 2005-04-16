#include "soc_common.h"
#include "sa11xx_base.h"

extern int sa1111_pcmcia_hw_init(struct soc_pcmcia_socket *);
extern void sa1111_pcmcia_hw_shutdown(struct soc_pcmcia_socket *);
extern void sa1111_pcmcia_socket_state(struct soc_pcmcia_socket *, struct pcmcia_state *);
extern int sa1111_pcmcia_configure_socket(struct soc_pcmcia_socket *, const socket_state_t *);
extern void sa1111_pcmcia_socket_init(struct soc_pcmcia_socket *);
extern void sa1111_pcmcia_socket_suspend(struct soc_pcmcia_socket *);

extern int pcmcia_badge4_init(struct device *);
extern int pcmcia_jornada720_init(struct device *);
extern int pcmcia_lubbock_init(struct sa1111_dev *);
extern int pcmcia_neponset_init(struct sa1111_dev *);

