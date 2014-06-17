/* procobjecttree.c
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include "procobjecttree.h"

#define MYDRVNAME "procobjecttree"



/** This is context info that we stash in each /proc file entry, which we
 *  need in order to call the callback function that supplies the /proc read
 *  info for that file.
 */
typedef struct {
	void (*show_property)(struct seq_file *, void *, int);
	MYPROCOBJECT *procObject;
	int propertyIndex;

} PROCDIRENTRYCONTEXT;

/** This describes the attributes of a tree rooted at
 *  <procDirRoot>/<name[0]>/<name[1]>/...
 *  Properties for each object of this type will be located under
 *  <procDirRoot>/<name[0]>/<name[1]>/.../<objectName>/<propertyName>.
 */
struct MYPROCTYPE_Tag {
	const char **name;  /**< node names for this type, ending with NULL */
	int nNames;         /**< num of node names in <name> */

	/** root dir for this type tree in /proc */
	struct proc_dir_entry *procDirRoot;

	struct proc_dir_entry **procDirs;  /**< for each node in <name> */

	/** bottom dir where objects will be rooted; i.e., this is
	 *  <procDirRoot>/<name[0]>/<name[1]>/.../, which is the same as the
	 *  last entry in the <procDirs> array. */
	struct proc_dir_entry *procDir;

	/** name for each property that objects of this type can have */
	const char **propertyNames;

	int nProperties;       /**< num of names in <propertyNames> */

	/** Call this, passing MYPROCOBJECT.context and the property index
	 *  whenever someone reads the proc entry */
	void (*show_property)(struct seq_file *, void *, int);
};



struct MYPROCOBJECT_Tag {
	MYPROCTYPE *type;

	/** This is the name of the dir node in /proc under which the
	 *  properties of this object will appear as files. */
	char *name;

	int namesize;   /**< number of bytes allocated for name */
	void *context;  /**< passed to MYPROCTYPE.show_property */

	/** <type.procDirRoot>/<type.name[0]>/<type.name[1]>/.../<name> */
	struct proc_dir_entry *procDir;

	/** a proc dir entry for each of the properties of the object;
	 *  properties are identified in MYPROCTYPE.propertyNames, so each of
	 *  the <procDirProperties> describes a single file like
	 *  <type.procDirRoot>/<type.name[0]>/<type.name[1]>/...
	 *           /<name>/<propertyName>
	 */
	struct proc_dir_entry **procDirProperties;

	/** this is a holding area for the context information that is needed
	 *  to run the /proc callback function */
	PROCDIRENTRYCONTEXT *procDirPropertyContexts;
};



static struct proc_dir_entry *
createProcDir(const char *name, struct proc_dir_entry *parent)
{
	struct proc_dir_entry *p = proc_mkdir_mode(name, S_IFDIR, parent);
	if (p == NULL)
		ERRDRV("failed to create /proc directory %s", name);
	return p;
}

static struct proc_dir_entry *
createProcFile(const char *name, struct proc_dir_entry *parent,
	       const struct file_operations *fops, void *data)
{
	struct proc_dir_entry *p = proc_create_data(name, 0, parent,
						    fops, data);
	if (p == NULL)
		ERRDRV("failed to create /proc file %s", name);
	return p;
}

static int seq_show(struct seq_file *seq, void *offset);
static int proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, seq_show, PDE_DATA(inode));
}

static const struct file_operations proc_fops = {
	.open = proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};



MYPROCTYPE *visor_proc_CreateType(struct proc_dir_entry *procDirRoot,
				  const char **name,
				  const char **propertyNames,
				  void (*show_property)(struct seq_file *,
							void *, int))
{
	int i = 0;
	MYPROCTYPE *rc = NULL, *type = NULL;
	struct proc_dir_entry *parent = NULL;

	if (procDirRoot == NULL) {
		ERRDRV("procDirRoot cannot be NULL!\n");
		goto Away;
	}
	if (name == NULL || name[0] == NULL) {
		ERRDRV("name must contain at least 1 node name!\n");
		goto Away;
	}
	type = kzalloc(sizeof(MYPROCTYPE), GFP_KERNEL | __GFP_NORETRY);
	if (type == NULL) {
		ERRDRV("out of memory\n");
		goto Away;
	}
	type->name = name;
	type->propertyNames = propertyNames;
	type->nProperties = 0;
	type->nNames = 0;
	type->show_property = show_property;
	type->procDirRoot = procDirRoot;
	if (type->propertyNames != NULL)
		while (type->propertyNames[type->nProperties] != NULL)
			type->nProperties++;
	while (type->name[type->nNames] != NULL)
		type->nNames++;
	type->procDirs = kzalloc((type->nNames + 1) *
				 sizeof(struct proc_dir_entry *),
				 GFP_KERNEL | __GFP_NORETRY);
	if (type->procDirs == NULL) {
		ERRDRV("out of memory\n");
		goto Away;
	}
	parent = procDirRoot;
	for (i = 0; i < type->nNames; i++) {
		type->procDirs[i] = createProcDir(type->name[i], parent);
		if (type->procDirs[i] == NULL) {
			rc = NULL;
			goto Away;
		}
		parent = type->procDirs[i];
	}
	type->procDir = type->procDirs[type->nNames-1];
	rc = type;
Away:
	if (rc == NULL) {
		if (type != NULL) {
			visor_proc_DestroyType(type);
			type = NULL;
		}
	}
	return rc;
}
EXPORT_SYMBOL_GPL(visor_proc_CreateType);



void visor_proc_DestroyType(MYPROCTYPE *type)
{
	if (type == NULL)
		return;
	if (type->procDirs != NULL) {
		int i = type->nNames-1;
		while (i >= 0) {
			if (type->procDirs[i] != NULL) {
				struct proc_dir_entry *parent = NULL;
				if (i == 0)
					parent = type->procDirRoot;
				else
					parent = type->procDirs[i-1];
				remove_proc_entry(type->name[i], parent);
			}
			i--;
		}
		kfree(type->procDirs);
		type->procDirs = NULL;
	}
	kfree(type);
}
EXPORT_SYMBOL_GPL(visor_proc_DestroyType);



MYPROCOBJECT *visor_proc_CreateObject(MYPROCTYPE *type,
				      const char *name, void *context)
{
	MYPROCOBJECT *obj = NULL, *rc = NULL;
	int i = 0;

	if (type == NULL) {
		ERRDRV("type cannot be NULL\n");
		goto Away;
	}
	obj = kzalloc(sizeof(MYPROCOBJECT), GFP_KERNEL | __GFP_NORETRY);
	if (obj == NULL) {
		ERRDRV("out of memory\n");
		goto Away;
	}
	obj->type = type;
	obj->context = context;
	if (name == NULL) {
		obj->name = NULL;
		obj->procDir = type->procDir;
	} else {
		obj->namesize = strlen(name)+1;
		obj->name = kmalloc(obj->namesize, GFP_KERNEL | __GFP_NORETRY);
		if (obj->name == NULL) {
			obj->namesize = 0;
			ERRDRV("out of memory\n");
			goto Away;
		}
		strcpy(obj->name, name);
		obj->procDir = createProcDir(obj->name, type->procDir);
		if (obj->procDir == NULL) {
			goto Away;
		}
	}
	obj->procDirPropertyContexts =
		kzalloc((type->nProperties + 1) * sizeof(PROCDIRENTRYCONTEXT),
			GFP_KERNEL | __GFP_NORETRY);
	if (obj->procDirPropertyContexts == NULL) {
		ERRDRV("out of memory\n");
		goto Away;
	}
	obj->procDirProperties =
		kzalloc((type->nProperties + 1) * sizeof(struct proc_dir_entry *),
			GFP_KERNEL | __GFP_NORETRY);
	if (obj->procDirProperties == NULL) {
		ERRDRV("out of memory\n");
		goto Away;
	}
	for (i = 0; i < type->nProperties; i++) {
		obj->procDirPropertyContexts[i].procObject = obj;
		obj->procDirPropertyContexts[i].propertyIndex = i;
		obj->procDirPropertyContexts[i].show_property =
			type->show_property;
		if (type->propertyNames[i][0] != '\0') {
			/* only create properties that have names */
			obj->procDirProperties[i] =
				createProcFile(type->propertyNames[i],
					       obj->procDir, &proc_fops,
					       &obj->procDirPropertyContexts[i]);
			if (obj->procDirProperties[i] == NULL) {
				rc = NULL;
				goto Away;
			}
		}
	}
	rc = obj;
Away:
	if (rc == NULL) {
		if (obj != NULL) {
			visor_proc_DestroyObject(obj);
			obj = NULL;
		}
	}
	return rc;
}
EXPORT_SYMBOL_GPL(visor_proc_CreateObject);



void visor_proc_DestroyObject(MYPROCOBJECT *obj)
{
	MYPROCTYPE *type = NULL;
	if (obj == NULL)
		return;
	type = obj->type;
	if (type == NULL)
		return;
	if (obj->procDirProperties != NULL) {
		int i = 0;
		for (i = 0; i < type->nProperties; i++) {
			if (obj->procDirProperties[i] != NULL) {
				remove_proc_entry(type->propertyNames[i],
						  obj->procDir);
				obj->procDirProperties[i] = NULL;
			}
		}
		kfree(obj->procDirProperties);
		obj->procDirProperties = NULL;
	}
	if (obj->procDirPropertyContexts != NULL) {
		kfree(obj->procDirPropertyContexts);
		obj->procDirPropertyContexts = NULL;
	}
	if (obj->procDir != NULL) {
		if (obj->name != NULL)
			remove_proc_entry(obj->name, type->procDir);
		obj->procDir = NULL;
	}
	if (obj->name != NULL) {
		kfree(obj->name);
		obj->name = NULL;
	}
	kfree(obj);
}
EXPORT_SYMBOL_GPL(visor_proc_DestroyObject);



static int seq_show(struct seq_file *seq, void *offset)
{
	PROCDIRENTRYCONTEXT *ctx = (PROCDIRENTRYCONTEXT *)(seq->private);
	if (ctx == NULL) {
		ERRDRV("I don't have a freakin' clue...");
		return 0;
	}
	(*ctx->show_property)(seq, ctx->procObject->context,
			      ctx->propertyIndex);
	return 0;
}
