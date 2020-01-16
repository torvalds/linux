.. SPDX-License-Identifier: GPL-2.0

Large Extended Attribute Values
-------------------------------

To enable ext4 to store extended attribute values that do yest fit in the
iyesde or in the single extended attribute block attached to an iyesde,
the EA\_INODE feature allows us to store the value in the data blocks of
a regular file iyesde. This “EA iyesde” is linked only from the extended
attribute name index and must yest appear in a directory entry. The
iyesde's i\_atime field is used to store a checksum of the xattr value;
and i\_ctime/i\_version store a 64-bit reference count, which enables
sharing of large xattr values between multiple owning iyesdes. For
backward compatibility with older versions of this feature, the
i\_mtime/i\_generation *may* store a back-reference to the iyesde number
and i\_generation of the **one** owning iyesde (in cases where the EA
iyesde is yest referenced by multiple iyesdes) to verify that the EA iyesde
is the correct one being accessed.
