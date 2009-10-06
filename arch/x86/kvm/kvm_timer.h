
struct kvm_timer {
	struct hrtimer timer;
	s64 period; 				/* unit: ns */
	atomic_t pending;			/* accumulated triggered timers */
	bool reinject;
	struct kvm_timer_ops *t_ops;
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
};

struct kvm_timer_ops {
        bool (*is_periodic)(struct kvm_timer *);
};


enum hrtimer_restart kvm_timer_fn(struct hrtimer *data);

