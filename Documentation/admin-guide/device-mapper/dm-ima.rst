======
dm-ima
======

For a given system, various external services/infrastructure tools
(including the attestation service) interact with it - both during the
setup and during rest of the system run-time.  They share sensitive data
and/or execute critical workload on that system.  The external services
may want to verify the current run-time state of the relevant kernel
subsystems before fully trusting the system with business-critical
data/workload.

Device mapper plays a critical role on a given system by providing
various important functionalities to the block devices using various
target types like crypt, verity, integrity etc.  Each of these target
types’ functionalities can be configured with various attributes.
The attributes chosen to configure these target types can significantly
impact the security profile of the block device, and in-turn, of the
system itself.  For instance, the type of encryption algorithm and the
key size determines the strength of encryption for a given block device.

Therefore, verifying the current state of various block devices as well
as their various target attributes is crucial for external services before
fully trusting the system with business-critical data/workload.

IMA kernel subsystem provides the necessary functionality for
device mapper to measure the state and configuration of
various block devices -
  - BY device mapper itself, from within the kernel,
  - in a tamper resistant way,
  - and re-measured - triggered on state/configuration change.

Setting the IMA Policy:
=======================
For IMA to measure the data on a given system, the IMA policy on the
system needs to be updated to have following line, and the system needs
to be restarted for the measurements to take effect.

/etc/ima/ima-policy
 measure func=CRITICAL_DATA label=device-mapper template=ima-buf

The measurements will be reflected in the IMA logs, which are located at:

/sys/kernel/security/integrity/ima/ascii_runtime_measurements
/sys/kernel/security/integrity/ima/binary_runtime_measurements

Then IMA ASCII measurement log has the following format:
PCR TEMPLATE_DIGEST TEMPLATE ALG:EVENT_DIGEST EVENT_NAME EVENT_DATA

PCR := Platform Configuration Register, in which the values are registered.
       This is applicable if TPM chip is in use.
TEMPLATE_DIGEST := Template digest of the IMA record.
TEMPLATE := Template that registered the integrity value (e.g. ima-buf).
ALG:EVENT_DIGEST = Algorithm to compute event digest, followed by digest of event data
EVENT_NAME := Description of the event (e.g. 'table_load').
EVENT_DATA := The event data to be measured.

The DM target data measured by IMA subsystem can alternatively
be queried from userspace by setting DM_IMA_MEASUREMENT_FLAG with
DM_TABLE_STATUS_CMD.

Supported Device States:
========================
Following device state changes will trigger IMA measurements.
01. Table load
02. Device resume
03. Device remove
04. Table clear
05. Device rename

01. Table load:
---------------
When a new table is loaded in a device's inactive table slot,
the device information and target specific details from the
targets in the table are measured.

For instance, if a linear device is created with the following table entries,
# dmsetup create linear1
0 2 linear /dev/loop0 512
2 2 linear /dev/loop0 512
4 2 linear /dev/loop0 512
6 2 linear /dev/loop0 512

Then IMA ASCII measurement log will have an entry with:
EVENT_NAME := table_load
EVENT_DATA := [device_data];[target_data_row_1];[target_data_row_2];...[target_data_row_n];

E.g.
(converted from ASCII to text for readability)
10 a8c5ff755561c7a28146389d1514c318592af49a ima-buf sha256:4d73481ecce5eadba8ab084640d85bb9ca899af4d0a122989252a76efadc5b72
table_load
name=linear1,uuid=,major=253,minor=0,minor_count=1,num_targets=4;
target_index=0,target_begin=0,target_len=2,target_name=linear,target_version=1.4.0,device_name=7:0,start=512;
target_index=1,target_begin=2,target_len=2,target_name=linear,target_version=1.4.0,device_name=7:0,start=512;
target_index=2,target_begin=4,target_len=2,target_name=linear,target_version=1.4.0,device_name=7:0,start=512;
target_index=3,target_begin=6,target_len=2,target_name=linear,target_version=1.4.0,device_name=7:0,start=512;

02. Device resume:
------------------
When a suspended device is resumed, the device information and a sha256 hash of the
data from previous load of an active table are measured.

For instance, if a linear device is resumed with the following command,
#dmsetup resume linear1

Then IMA ASCII measurement log will have an entry with:
EVENT_NAME := device_resume
EVENT_DATA := [device_data];active_table_hash=(sha256hash([device_data];[target_data_row_1];...[target_data_row_n]);
              current_device_capacity=<N>;

E.g.
(converted from ASCII to text for readability)
10 56c00cc062ffc24ccd9ac2d67d194af3282b934e ima-buf sha256:e7d12c03b958b4e0e53e7363a06376be88d98a1ac191fdbd3baf5e4b77f329b6
device_resume
name=linear1,uuid=,major=253,minor=0,minor_count=1,num_targets=4;
active_table_hash=4d73481ecce5eadba8ab084640d85bb9ca899af4d0a122989252a76efadc5b72;current_device_capacity=8;

03. Device remove:
------------------
When a device is removed, the device information and a sha256 hash of the
data from an active and inactive table are measured.

For instance, if a linear device is removed with the following command,
# dmsetup remove linear1

Then IMA ASCII measurement log will have an entry with:
EVENT_NAME := device_remove
EVENT_DATA := [device_active_metadata];[device_inactive_metadata];
              [active_table_hash=(sha256hash([device_active_metadata];[active_table_row_1];...[active_table_row_n]),
              [inactive_table_hash=(sha256hash([device_inactive_metadata];[inactive_table_row_1];...[inactive_table_row_n]),
              remove_all=[y|n];current_device_capacity=<N>;

E.g
(converted from ASCII to text for readability)
10 499812b621b705061c4514d643894483e16d2619 ima-buf sha256:c3f26b02f09bf5b464925589454bdd4d354077ce430fd1e75c9e96ce29cd1cad
device_remove
device_active_metadata=name=linear1,uuid=,major=253,minor=0,minor_count=1,num_targets=4;
device_inactive_metadata=name=linear1,uuid=,major=253,minor=0,minor_count=1,num_targets=2;
active_table_hash=4d73481ecce5eadba8ab084640d85bb9ca899af4d0a122989252a76efadc5b72,
inactive_table_hash=5596cc857b0e887fd0c5d58dc6382513284596b07f09fd37efae2da224bd521d,remove_all=n;
current_device_capacity=8;


04. Table clear:
----------------
When an inactive table is cleared from the device, the device information and a sha256 hash of the
data from an inactive table are measured.

For instance, if a linear device's inactive table is cleared with the following command,

# dmsetup clear linear1

Then IMA ASCII measurement log will have an entry with:
EVENT_NAME := table_clear
EVENT_DATA := [device_data];inactive_table_hash=(sha256hash([device_data];[inactive_table_row_1];...[inactive_table_row_n]);
current_device_capacity=<N>;

E.g.
(converted from ASCII to text for readability)
10 9c11e284d792875352d51c09f6643c96649484be ima-buf sha256:84b22b364ea4d8264fa33c38635c18ef448fa9077731fa7e5f969b1da2003ea4
table_clear
name=linear1,uuid=,major=253,minor=0,minor_count=1,num_targets=2;
inactive_table_hash=5596cc857b0e887fd0c5d58dc6382513284596b07f09fd37efae2da224bd521d;current_device_capacity=0;


05. Device rename:
------------------
When an device's NAME or UUID is changed, the device information and the new NAME and UUID
are measured.

For instance, if a linear device's name is changed with the following command,

#dmsetup rename linear1 linear=2
Then IMA ASCII measurement log will have an entry with:
EVENT_NAME := device_rename
EVENT_DATA := [current_device_data];new_name=<new_name_value>;new_uuid=<new_uuid_value>;current_device_capacity=<N>;

E.g 1:
#dmsetup rename linear1 --setuuid 1234-5678

IMA Log entry:
(converted from ASCII to text for readability)
10 7380ef4d1349fe1ebd74affa54e9fcc960e3cbf5 ima-buf sha256:9759e36a17a967ea43c1bf3455279395a40bd0401105ec5ad8edb9a52054efc7
device_rename
name=linear1,uuid=,major=253,minor=0,minor_count=1,num_targets=1;new_name=linear1,new_uuid=1234-5678;current_device_capacity=2;

E.g 2:
# dmsetup rename linear1 linear=2
10 092c8266fc36e44f74c59f123ecfe15310f249f4 ima-buf sha256:4cf8b85c81fa6fedaeb602b05019124dbbb0605dce58fcdeea56887a8a3874cd
device_rename
name=linear1,uuid=1234-5678,major=253,minor=0,minor_count=1,num_targets=1;new_name=linear\=2,new_uuid=1234-5678;current_device_capacity=2;


Supported targets:
==================
Following targets are supported to measure their data using IMA.

01. cache
02. crypt
03. integrity
04. linear
05. mirror
06. multipath
07. raid
08. snapshot
09. striped
10. verity

01. cache
---------
<<documenatation in progress>>

02. crypt
---------
When a crypt target is loaded, then IMA ASCII measurement log will have an entry
similar to the following, depicting what crypt attributes are measured in EVENT_DATA.

(converted from ASCII to text for readability)
10 fe3b80a35b155bd282df778e2625066c05fc068c ima-buf sha256:2d86ce9d6f16a4a97607318aa123ae816e0ceadefeea7903abf7f782f2cb78ad
table_load
name=test-crypt,uuid=,major=253,minor=0,minor_count=1,num_targets=1;
target_index=0,target_begin=0,target_len=1953125,target_name=crypt,target_version=1.23.0,
allow_discards=y,same_cpu=n,submit_from_crypt_cpus=n,no_read_workqueue=n,no_write_workqueue=n,
iv_large_sectors=n,cipher_string=aes-xts-plain64,key_size=32,key_parts=1,key_extra_size=0,key_mac_size=0;

03. integrity
-------------
<<documenatation in progress>>


04. linear
----------
When a linear target is loaded, then IMA ASCII measurement log will have an entry
similar to the following, depicting what linear attributes are measured in EVENT_DATA.

(converted from ASCII to text for readability)
10 a8c5ff755561c7a28146389d1514c318592af49a ima-buf sha256:4d73481ecce5eadba8ab084640d85bb9ca899af4d0a122989252a76efadc5b72
table_load
name=linear1,uuid=,major=253,minor=0,minor_count=1,num_targets=4;
target_index=0,target_begin=0,target_len=2,target_name=linear,target_version=1.4.0,device_name=7:0,start=512;
target_index=1,target_begin=2,target_len=2,target_name=linear,target_version=1.4.0,device_name=7:0,start=512;
target_index=2,target_begin=4,target_len=2,target_name=linear,target_version=1.4.0,device_name=7:0,start=512;
target_index=3,target_begin=6,target_len=2,target_name=linear,target_version=1.4.0,device_name=7:0,start=512;

05. mirror
----------
When a mirror target is loaded, then IMA ASCII measurement log will have an entry
similar to the following, depicting what mirror attributes are measured in EVENT_DATA.

(converted from ASCII to text for readability)
10 90ff9113a00c367df823595dc347425ce3bfc50a ima-buf sha256:8da0678ed3bf616533573d9e61e5342f2bd16cb0b3145a08262641a743806c2e
table_load
name=test-mirror,uuid=,major=253,minor=4,minor_count=1,num_targets=1;
target_index=0,target_begin=0,target_len=1953125,target_name=mirror,target_version=1.14.0,
nr_mirrors=2,mirror_device_0=253:2,mirror_device_0_status=A,mirror_device_1=253:3,mirror_device_1_status=A,
handle_errors=y,keep_log=n,log_type_status=;

06. multipath
-------------
<<documenatation in progress>>

07. raid
--------
When a raid target is loaded, then IMA ASCII measurement log will have an entry
similar to the following, depicting what raid attributes are measured in EVENT_DATA.

(converted from ASCII to text for readability)
10 76cb30d0cd0fe099966f20f5c82e3a2ac29b21a0 ima-buf sha256:52250f20b27376fcfb348bdfa1e1cf5acfd6646e0f3ad1a72952cffd9f818753
table_load
name=test-raid1,uuid=,major=253,minor=2,minor_count=1,num_targets=1;
target_index=0,target_begin=0,target_len=1953125,target_name=raid,target_version=1.15.1,
raid_type=raid1,raid_disks=2,raid_state=idle,raid_device_0_status=A,raid_device_1_status=A;

08. snapshot
------------
<<documenatation in progress>>

09. striped
-----------
When a linear target is loaded, then IMA ASCII measurement log will have an entry
similar to the following, depicting what linear attributes are measured in EVENT_DATA.

(converted from ASCII to text for readability)
10 7bd94fa8f799169b9f12d97b9dbdce4dc5509233 ima-buf sha256:0d148eda69887f7833f1a6042767b54359cd23b64fa941b9e1856879eee1f778
table_load
name=test-raid0,uuid=,major=253,minor=8,minor_count=1,num_targets=1;
target_index=0,target_begin=0,target_len=7812096,target_name=striped,target_version=1.6.0,stripes=4,chunk_size=128,
stripe_0_device_name=253:1,stripe_0_physical_start=0,stripe_0_status=A,
stripe_1_device_name=253:3,stripe_1_physical_start=0,stripe_1_status=A,
stripe_2_device_name=253:5,stripe_2_physical_start=0,stripe_2_status=A,
stripe_3_device_name=253:7,stripe_3_physical_start=0,stripe_3_status=A;

10. verity
----------
When a verity target is loaded, then IMA ASCII measurement log will have an entry
similar to the following, depicting what verity attributes are measured in EVENT_DATA.

(converted from ASCII to text for readability)
10 fced5f575b140fc0efac302c88a635174cd663da ima-buf sha256:021370c1cc93929460b06922c606334fb1d7ea5ecf04f2384f3157a446894283
table_load
name=test-verity,uuid=,major=253,minor=2,minor_count=1,num_targets=1;
target_index=0,target_begin=0,target_len=1953120,target_name=verity,target_version=1.8.0,hash_failed=V,
verity_version=1,data_device_name=253:1,hash_device_name=253:0,verity_algorithm=sha256,
root_digest=29cb87e60ce7b12b443ba6008266f3e41e93e403d7f298f8e3f316b29ff89c5e,
salt=e48da609055204e89ae53b655ca2216dd983cf3cb829f34f63a297d106d53e2d,
ignore_zero_blocks=n,check_at_most_once=n;
