#ifndef DRBD_STATE_CHANGE_H
#define DRBD_STATE_CHANGE_H

struct drbd_resource_state_change {
	struct drbd_resource *resource;
	enum drbd_role role[2];
	bool susp[2];
	bool susp_nod[2];
	bool susp_fen[2];
};

struct drbd_device_state_change {
	struct drbd_device *device;
	enum drbd_disk_state disk_state[2];
};

struct drbd_connection_state_change {
	struct drbd_connection *connection;
	enum drbd_conns cstate[2];  /* drbd9: enum drbd_conn_state */
	enum drbd_role peer_role[2];
};

struct drbd_peer_device_state_change {
	struct drbd_peer_device *peer_device;
	enum drbd_disk_state disk_state[2];
	enum drbd_conns repl_state[2];  /* drbd9: enum drbd_repl_state */
	bool resync_susp_user[2];
	bool resync_susp_peer[2];
	bool resync_susp_dependency[2];
};

struct drbd_state_change {
	struct list_head list;
	unsigned int n_devices;
	unsigned int n_connections;
	struct drbd_resource_state_change resource[1];
	struct drbd_device_state_change *devices;
	struct drbd_connection_state_change *connections;
	struct drbd_peer_device_state_change *peer_devices;
};

extern struct drbd_state_change *remember_old_state(struct drbd_resource *, gfp_t);
extern void copy_old_to_new_state_change(struct drbd_state_change *);
extern void forget_state_change(struct drbd_state_change *);

extern void notify_resource_state_change(struct sk_buff *,
					 unsigned int,
					 struct drbd_resource_state_change *,
					 enum drbd_notification_type type);
extern void notify_connection_state_change(struct sk_buff *,
					   unsigned int,
					   struct drbd_connection_state_change *,
					   enum drbd_notification_type type);
extern void notify_device_state_change(struct sk_buff *,
				       unsigned int,
				       struct drbd_device_state_change *,
				       enum drbd_notification_type type);
extern void notify_peer_device_state_change(struct sk_buff *,
					    unsigned int,
					    struct drbd_peer_device_state_change *,
					    enum drbd_notification_type type);

#endif  /* DRBD_STATE_CHANGE_H */
