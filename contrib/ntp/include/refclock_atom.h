/*
 * Definitions for the atom driver and its friends
 */
#undef NANOSECOND	/* some systems define it differently */
#define NANOSECOND	1000000000 /* one second (ns) */

struct refclock_atom {
	pps_handle_t handle;
	pps_params_t pps_params;
	struct timespec ts;
};

extern	int	refclock_ppsapi(int, struct refclock_atom *);
extern	int	refclock_params(int, struct refclock_atom *);
extern	int	refclock_pps(struct peer *, struct refclock_atom *, int);
