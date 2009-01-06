#ifndef __ASM_NUMAQ_IPI_H
#define __ASM_NUMAQ_IPI_H

void send_IPI_mask_sequence(const struct cpumask *mask, int vector);
void send_IPI_mask_allbutself(const struct cpumask *mask, int vector);

static inline void send_IPI_mask(const struct cpumask *mask, int vector)
{
	send_IPI_mask_sequence(mask, vector);
}

static inline void send_IPI_allbutself(int vector)
{
	send_IPI_mask_allbutself(cpu_online_mask, vector);
}

static inline void send_IPI_all(int vector)
{
	send_IPI_mask(cpu_online_mask, vector);
}

#endif /* __ASM_NUMAQ_IPI_H */
