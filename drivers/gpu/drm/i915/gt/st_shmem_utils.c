// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

/* Just a quick and causal check of the shmem_utils API */

static int igt_shmem_basic(void *ignored)
{
	u32 datum = 0xdeadbeef, result;
	struct file *file;
	u32 *map;
	int err;

	file = shmem_create_from_data("mock", &datum, sizeof(datum));
	if (IS_ERR(file))
		return PTR_ERR(file);

	result = 0;
	err = shmem_read(file, 0, &result, sizeof(result));
	if (err)
		goto out_file;

	if (result != datum) {
		pr_err("Incorrect read back from shmemfs: %x != %x\n",
		       result, datum);
		err = -EINVAL;
		goto out_file;
	}

	result = 0xc0ffee;
	err = shmem_write(file, 0, &result, sizeof(result));
	if (err)
		goto out_file;

	map = shmem_pin_map(file);
	if (!map) {
		err = -ENOMEM;
		goto out_file;
	}

	if (*map != result) {
		pr_err("Incorrect read back via mmap of last write: %x != %x\n",
		       *map, result);
		err = -EINVAL;
		goto out_map;
	}

out_map:
	shmem_unpin_map(file, map);
out_file:
	fput(file);
	return err;
}

int shmem_utils_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_shmem_basic),
	};

	return i915_subtests(tests, NULL);
}
