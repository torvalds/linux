.. SPDX-License-Identifier: GPL-2.0

struct sk_buff
==============

:c:type:`sk_buff` is the main networking structure representing
a packet.

Basic sk_buff geometry
----------------------

.. kernel-doc:: include/linux/skbuff.h
   :doc: Basic sk_buff geometry

Shared skbs and skb clones
--------------------------

:c:member:`sk_buff.users` is a simple refcount allowing multiple entities
to keep a struct sk_buff alive. skbs with a ``sk_buff.users != 1`` are referred
to as shared skbs (see skb_shared()).

skb_clone() allows for fast duplication of skbs. None of the data buffers
get copied, but caller gets a new metadata struct (struct sk_buff).
&skb_shared_info.refcount indicates the number of skbs pointing at the same
packet data (i.e. clones).

dataref and headerless skbs
---------------------------

.. kernel-doc:: include/linux/skbuff.h
   :doc: dataref and headerless skbs

Checksum information
--------------------

.. kernel-doc:: include/linux/skbuff.h
   :doc: skb checksums
