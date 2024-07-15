Linux Devlink Documentation
===========================

devlink is an API to expose device information and resources not directly
related to any device class, such as chip-wide/switch-ASIC-wide configuration.

Locking
-------

Driver facing APIs are currently transitioning to allow more explicit
locking. Drivers can use the existing ``devlink_*`` set of APIs, or
new APIs prefixed by ``devl_*``. The older APIs handle all the locking
in devlink core, but don't allow registration of most sub-objects once
the main devlink object is itself registered. The newer ``devl_*`` APIs assume
the devlink instance lock is already held. Drivers can take the instance
lock by calling ``devl_lock()``. It is also held all callbacks of devlink
netlink commands.

Drivers are encouraged to use the devlink instance lock for their own needs.

Drivers need to be cautious when taking devlink instance lock and
taking RTNL lock at the same time. Devlink instance lock needs to be taken
first, only after that RTNL lock could be taken.

Nested instances
----------------

Some objects, like linecards or port functions, could have another
devlink instances created underneath. In that case, drivers should make
sure to respect following rules:

 - Lock ordering should be maintained. If driver needs to take instance
   lock of both nested and parent instances at the same time, devlink
   instance lock of the parent instance should be taken first, only then
   instance lock of the nested instance could be taken.
 - Driver should use object-specific helpers to setup the
   nested relationship:

   - ``devl_nested_devlink_set()`` - called to setup devlink -> nested
     devlink relationship (could be user for multiple nested instances.
   - ``devl_port_fn_devlink_set()`` - called to setup port function ->
     nested devlink relationship.
   - ``devlink_linecard_nested_dl_set()`` - called to setup linecard ->
     nested devlink relationship.

The nested devlink info is exposed to the userspace over object-specific
attributes of devlink netlink.

Interface documentation
-----------------------

The following pages describe various interfaces available through devlink in
general.

.. toctree::
   :maxdepth: 1

   devlink-dpipe
   devlink-health
   devlink-info
   devlink-flash
   devlink-params
   devlink-port
   devlink-region
   devlink-resource
   devlink-reload
   devlink-selftests
   devlink-trap
   devlink-linecard
   devlink-eswitch-attr

Driver-specific documentation
-----------------------------

Each driver that implements ``devlink`` is expected to document what
parameters, info versions, and other features it supports.

.. toctree::
   :maxdepth: 1

   bnxt
   etas_es58x
   hns3
   i40e
   ionic
   ice
   mlx4
   mlx5
   mlxsw
   mv88e6xxx
   netdevsim
   nfp
   qed
   ti-cpsw-switch
   am65-nuss-cpsw-switch
   prestera
   iosm
   octeontx2
   sfc
