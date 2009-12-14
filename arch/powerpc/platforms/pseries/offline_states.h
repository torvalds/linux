#ifndef _OFFLINE_STATES_H_
#define _OFFLINE_STATES_H_

/* Cpu offline states go here */
enum cpu_state_vals {
	CPU_STATE_OFFLINE,
	CPU_STATE_INACTIVE,
	CPU_STATE_ONLINE,
	CPU_MAX_OFFLINE_STATES
};

extern enum cpu_state_vals get_cpu_current_state(int cpu);
extern void set_cpu_current_state(int cpu, enum cpu_state_vals state);
extern enum cpu_state_vals get_preferred_offline_state(int cpu);
extern void set_preferred_offline_state(int cpu, enum cpu_state_vals state);
extern void set_default_offline_state(int cpu);
extern int start_secondary(void);
#endif
