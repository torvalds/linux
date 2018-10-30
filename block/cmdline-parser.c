// SPDX-License-Identifier: GPL-2.0
/*
 * Parse command line, get partition information
 *
 * Written by Cai Zhiyong <caizhiyong@huawei.com>
 *
 */
#include <linux/export.h>
#include <linux/cmdline-parser.h>

static int parse_subpart(struct cmdline_subpart **subpart, char *partdef)
{
	int ret = 0;
	struct cmdline_subpart *new_subpart;

	*subpart = NULL;

	new_subpart = kzalloc(sizeof(struct cmdline_subpart), GFP_KERNEL);
	if (!new_subpart)
		return -ENOMEM;

	if (*partdef == '-') {
		new_subpart->size = (sector_t)(~0ULL);
		partdef++;
	} else {
		new_subpart->size = (sector_t)memparse(partdef, &partdef);
		if (new_subpart->size < (sector_t)PAGE_SIZE) {
			pr_warn("cmdline partition size is invalid.");
			ret = -EINVAL;
			goto fail;
		}
	}

	if (*partdef == '@') {
		partdef++;
		new_subpart->from = (sector_t)memparse(partdef, &partdef);
	} else {
		new_subpart->from = (sector_t)(~0ULL);
	}

	if (*partdef == '(') {
		int length;
		char *next = strchr(++partdef, ')');

		if (!next) {
			pr_warn("cmdline partition format is invalid.");
			ret = -EINVAL;
			goto fail;
		}

		length = min_t(int, next - partdef,
			       sizeof(new_subpart->name) - 1);
		strncpy(new_subpart->name, partdef, length);
		new_subpart->name[length] = '\0';

		partdef = ++next;
	} else
		new_subpart->name[0] = '\0';

	new_subpart->flags = 0;

	if (!strncmp(partdef, "ro", 2)) {
		new_subpart->flags |= PF_RDONLY;
		partdef += 2;
	}

	if (!strncmp(partdef, "lk", 2)) {
		new_subpart->flags |= PF_POWERUP_LOCK;
		partdef += 2;
	}

	*subpart = new_subpart;
	return 0;
fail:
	kfree(new_subpart);
	return ret;
}

static void free_subpart(struct cmdline_parts *parts)
{
	struct cmdline_subpart *subpart;

	while (parts->subpart) {
		subpart = parts->subpart;
		parts->subpart = subpart->next_subpart;
		kfree(subpart);
	}
}

static int parse_parts(struct cmdline_parts **parts, const char *bdevdef)
{
	int ret = -EINVAL;
	char *next;
	int length;
	struct cmdline_subpart **next_subpart;
	struct cmdline_parts *newparts;
	char buf[BDEVNAME_SIZE + 32 + 4];

	*parts = NULL;

	newparts = kzalloc(sizeof(struct cmdline_parts), GFP_KERNEL);
	if (!newparts)
		return -ENOMEM;

	next = strchr(bdevdef, ':');
	if (!next) {
		pr_warn("cmdline partition has no block device.");
		goto fail;
	}

	length = min_t(int, next - bdevdef, sizeof(newparts->name) - 1);
	strncpy(newparts->name, bdevdef, length);
	newparts->name[length] = '\0';
	newparts->nr_subparts = 0;

	next_subpart = &newparts->subpart;

	while (next && *(++next)) {
		bdevdef = next;
		next = strchr(bdevdef, ',');

		length = (!next) ? (sizeof(buf) - 1) :
			min_t(int, next - bdevdef, sizeof(buf) - 1);

		strncpy(buf, bdevdef, length);
		buf[length] = '\0';

		ret = parse_subpart(next_subpart, buf);
		if (ret)
			goto fail;

		newparts->nr_subparts++;
		next_subpart = &(*next_subpart)->next_subpart;
	}

	if (!newparts->subpart) {
		pr_warn("cmdline partition has no valid partition.");
		ret = -EINVAL;
		goto fail;
	}

	*parts = newparts;

	return 0;
fail:
	free_subpart(newparts);
	kfree(newparts);
	return ret;
}

void cmdline_parts_free(struct cmdline_parts **parts)
{
	struct cmdline_parts *next_parts;

	while (*parts) {
		next_parts = (*parts)->next_parts;
		free_subpart(*parts);
		kfree(*parts);
		*parts = next_parts;
	}
}
EXPORT_SYMBOL(cmdline_parts_free);

int cmdline_parts_parse(struct cmdline_parts **parts, const char *cmdline)
{
	int ret;
	char *buf;
	char *pbuf;
	char *next;
	struct cmdline_parts **next_parts;

	*parts = NULL;

	next = pbuf = buf = kstrdup(cmdline, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	next_parts = parts;

	while (next && *pbuf) {
		next = strchr(pbuf, ';');
		if (next)
			*next = '\0';

		ret = parse_parts(next_parts, pbuf);
		if (ret)
			goto fail;

		if (next)
			pbuf = ++next;

		next_parts = &(*next_parts)->next_parts;
	}

	if (!*parts) {
		pr_warn("cmdline partition has no valid partition.");
		ret = -EINVAL;
		goto fail;
	}

	ret = 0;
done:
	kfree(buf);
	return ret;

fail:
	cmdline_parts_free(parts);
	goto done;
}
EXPORT_SYMBOL(cmdline_parts_parse);

struct cmdline_parts *cmdline_parts_find(struct cmdline_parts *parts,
					 const char *bdev)
{
	while (parts && strncmp(bdev, parts->name, sizeof(parts->name)))
		parts = parts->next_parts;
	return parts;
}
EXPORT_SYMBOL(cmdline_parts_find);

/*
 *  add_part()
 *    0 success.
 *    1 can not add so many partitions.
 */
int cmdline_parts_set(struct cmdline_parts *parts, sector_t disk_size,
		      int slot,
		      int (*add_part)(int, struct cmdline_subpart *, void *),
		      void *param)
{
	sector_t from = 0;
	struct cmdline_subpart *subpart;

	for (subpart = parts->subpart; subpart;
	     subpart = subpart->next_subpart, slot++) {
		if (subpart->from == (sector_t)(~0ULL))
			subpart->from = from;
		else
			from = subpart->from;

		if (from >= disk_size)
			break;

		if (subpart->size > (disk_size - from))
			subpart->size = disk_size - from;

		from += subpart->size;

		if (add_part(slot, subpart, param))
			break;
	}

	return slot;
}
EXPORT_SYMBOL(cmdline_parts_set);
