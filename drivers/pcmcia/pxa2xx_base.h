/* temporary measure */
extern int __pxa2xx_drv_pcmcia_probe(struct device *);

int pxa2xx_drv_pcmcia_add_one(struct soc_pcmcia_socket *skt);
void pxa2xx_drv_pcmcia_ops(struct pcmcia_low_level *ops);

