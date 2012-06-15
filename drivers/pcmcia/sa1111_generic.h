#include "soc_common.h"
#include "sa11xx_base.h"

struct sa1111_pcmcia_socket {
	struct soc_pcmcia_socket soc;
	struct sa1111_dev *dev;
	struct sa1111_pcmcia_socket *next;
};

static inline struct sa1111_pcmcia_socket *to_skt(struct soc_pcmcia_socket *s)
{
	return container_of(s, struct sa1111_pcmcia_socket, soc);
}

int sa1111_pcmcia_add(struct sa1111_dev *dev, struct pcmcia_low_level *ops,
	int (*add)(struct soc_pcmcia_socket *));

extern void sa1111_pcmcia_socket_state(struct soc_pcmcia_socket *, struct pcmcia_state *);
extern int sa1111_pcmcia_configure_socket(struct soc_pcmcia_socket *, const socket_state_t *);

extern int pcmcia_badge4_init(struct device *);
extern int pcmcia_jornada720_init(struct device *);
extern int pcmcia_lubbock_init(struct sa1111_dev *);
extern int pcmcia_neponset_init(struct sa1111_dev *);

