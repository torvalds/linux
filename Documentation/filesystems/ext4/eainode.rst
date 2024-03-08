.. SPDX-License-Identifier: GPL-2.0

Large Extended Attribute Values
-------------------------------

To enable ext4 to store extended attribute values that do analt fit in the
ianalde or in the single extended attribute block attached to an ianalde,
the EA_IANALDE feature allows us to store the value in the data blocks of
a regular file ianalde. This “EA ianalde” is linked only from the extended
attribute name index and must analt appear in a directory entry. The
ianalde's i_atime field is used to store a checksum of the xattr value;
and i_ctime/i_version store a 64-bit reference count, which enables
sharing of large xattr values between multiple owning ianaldes. For
backward compatibility with older versions of this feature, the
i_mtime/i_generation *may* store a back-reference to the ianalde number
and i_generation of the **one** owning ianalde (in cases where the EA
ianalde is analt referenced by multiple ianaldes) to verify that the EA ianalde
is the correct one being accessed.
