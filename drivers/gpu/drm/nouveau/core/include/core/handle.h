#ifndef __NOUVEAU_HANDLE_H__
#define __NOUVEAU_HANDLE_H__

struct nouveau_handle {
	struct nouveau_namedb *namedb;
	struct list_head node;

	struct list_head head;
	struct list_head tree;
	u32 name;
	u32 priv;

	struct nouveau_handle *parent;
	struct nouveau_object *object;
};

int  nouveau_handle_create(struct nouveau_object *, u32 parent, u32 handle,
			   struct nouveau_object *, struct nouveau_handle **);
void nouveau_handle_destroy(struct nouveau_handle *);
int  nouveau_handle_init(struct nouveau_handle *);
int  nouveau_handle_fini(struct nouveau_handle *, bool suspend);

struct nouveau_object *
nouveau_handle_ref(struct nouveau_object *, u32 name);

#endif
