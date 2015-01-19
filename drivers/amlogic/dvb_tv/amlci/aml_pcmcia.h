
#ifndef _AML_PCMCIA_
#define _AML_PCMCIA_

enum aml_slot_state {
	MODULE_INSERTED			= 3,
	MODULE_XTRACTED			= 4
};

struct aml_pcmcia{
	enum aml_slot_state		slot_state;
	struct work_struct		pcmcia_work;

	int irq;
	int (*init_irq)(int flag);
	int (*get_cd1)(void);
	int (*get_cd2)(void);
	int (*pwr)(int enable);
	int (*rst)(int enable);

	int (*pcmcia_plugin)(struct aml_pcmcia *pc, int plugin);

	void *priv;
};

int aml_pcmcia_init(struct aml_pcmcia *pc);
int aml_pcmcia_exit(struct aml_pcmcia *pc);
int aml_pcmcia_reset(struct aml_pcmcia *pc);


#endif /*_AML_PCMCIA_*/

