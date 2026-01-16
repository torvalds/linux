.. SPDX-License-Identifier: GPL-2.0

==============================
Revocable Resource Management
==============================

Overview
========

.. kernel-doc:: drivers/base/revocable.c
   :doc: Overview

Revocable vs. Devres (devm)
===========================

It's important to understand the distinct roles of the Revocable and Devres,
and how they can complement each other.  They address different problems in
resource management:

*   **Devres:** Primarily address **resource leaks**.  The lifetime of the
    resources is tied to the lifetime of the device.  The resource is
    automatically freed when the device is unbound.  This cleanup happens
    irrespective of any potential active users.

*   **Revocable:** Primarily addresses **invalid memory access**,
    such as Use-After-Free (UAF).  It's an independent synchronization
    primitive that decouples consumer access from the resource's actual
    presence.  Consumers interact with a "revocable object" (an intermediary),
    not the underlying resource directly.  This revocable object persists as
    long as there are active references to it from consumer handles.

**Key Distinctions & How They Complement Each Other:**

1.  **Reference Target:** Consumers of a resource managed by the Revocable
    mechanism hold a reference to the *revocable object*, not the
    encapsulated resource itself.

2.  **Resource Lifetime vs. Access:** The underlying resource's lifetime is
    independent of the number of references to the revocable object.  The
    resource can be freed at any point.  A common scenario is the resource
    being freed by `devres` when the providing device is unbound.

3.  **Safe Access:** Revocable provides a safe way to attempt access.  Before
    using the resource, a consumer uses the Revocable API (e.g.,
    revocable_try_access()).  This function checks if the resource is still
    valid.  It returns a pointer to the resource only if it hasn't been
    revoked; otherwise, it returns NULL.  This prevents UAF by providing a
    clear signal that the resource is gone.

4.  **Complementary Usage:** `devres` and Revocable work well together.
    `devres` can handle the automatic allocation and deallocation of a
    resource tied to a device.  The Revocable mechanism can be layered on top
    to provide safe access for consumers whose lifetimes might extend beyond
    the provider device's lifetime.  For instance, a userspace program might
    keep a character device file open even after the physical device has been
    removed.  In this case:

    *   `devres` frees the device-specific resource upon unbinding.
    *   The Revocable mechanism ensures that any subsequent operations on the
        open file handle, which attempt to access the now-freed resource,
        will fail gracefully (e.g., revocable_try_access() returns NULL)
        instead of causing a UAF.

In summary, `devres` ensures resources are *released* to prevent leaks, while
the Revocable mechanism ensures that attempts to *access* these resources are
done safely, even if the resource has been released.

API and Usage
=============

For Resource Providers
----------------------
.. kernel-doc:: drivers/base/revocable.c
   :identifiers: revocable_provider

.. kernel-doc:: drivers/base/revocable.c
   :identifiers: revocable_provider_alloc

.. kernel-doc:: drivers/base/revocable.c
   :identifiers: devm_revocable_provider_alloc

.. kernel-doc:: drivers/base/revocable.c
   :identifiers: revocable_provider_revoke

For Resource Consumers
----------------------
.. kernel-doc:: drivers/base/revocable.c
   :identifiers: revocable

.. kernel-doc:: drivers/base/revocable.c
   :identifiers: revocable_alloc

.. kernel-doc:: drivers/base/revocable.c
   :identifiers: revocable_free

.. kernel-doc:: drivers/base/revocable.c
   :identifiers: revocable_try_access

.. kernel-doc:: drivers/base/revocable.c
   :identifiers: revocable_withdraw_access

.. kernel-doc:: include/linux/revocable.h
   :identifiers: REVOCABLE_TRY_ACCESS_WITH

Example Usage
~~~~~~~~~~~~~

.. code-block:: c

    void consumer_use_resource(struct revocable *rev)
    {
        struct foo_resource *res;

        REVOCABLE_TRY_ACCESS_WITH(rev, res);
        // Always check if the resource is valid.
        if (!res) {
            pr_warn("Resource is not available\n");
            return;
        }

        // At this point, 'res' is guaranteed to be valid until
        // this block exits.
        do_something_with(res);

    } // revocable_withdraw_access() is automatically called here.

.. kernel-doc:: include/linux/revocable.h
   :identifiers: REVOCABLE_TRY_ACCESS_SCOPED

Example Usage
~~~~~~~~~~~~~

.. code-block:: c

    void consumer_use_resource(struct revocable *rev)
    {
        struct foo_resource *res;

        REVOCABLE_TRY_ACCESS_SCOPED(rev, res) {
            // Always check if the resource is valid.
            if (!res) {
                pr_warn("Resource is not available\n");
                return;
            }

            // At this point, 'res' is guaranteed to be valid until
            // this block exits.
            do_something_with(res);
        }

        // revocable_withdraw_access() is automatically called here.
    }
