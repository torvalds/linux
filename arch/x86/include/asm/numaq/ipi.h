#ifndef __ASM_NUMAQ_IPI_H
#define __ASM_NUMAQ_IPI_H

void default_send_IPI_mask_sequence(const struct cpumask *mask, int vector);
void default_send_IPI_mask_allbutself(const struct cpumask *mask, int vector);

static inline void numaq_send_IPI_mask(const struct cpumask *mask, int vector)
{
	default_send_IPI_mask_sequence(mask, vector);
}

static inline void numaq_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself(cpu_online_mask, vector);
}

static inline void numaq_send_IPI_all(int vector)
{
	numaq_send_IPI_mask(cpu_online_mask, vector);
}

#endif /* __ASM_NUMAQ_IPI_H */
