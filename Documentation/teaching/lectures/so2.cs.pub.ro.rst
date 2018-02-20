====================
Sisteme de operare 2
====================

`View slides <so2.cs.pub.ro-slides.html>`_

.. slideconf::
   :autoslides: True
   :theme: single-level


The team
========

* Daniel Băluță (Daniel), Răzvan Deaconescu (Răzvan, RD), Valentin
  Ghiță (Vali), Alexandra Săndulescu (Alexandra)
* Mult succes în noul semestru!

Where do we stand?
==================

.. ditaa::

   +---------------------------------------------------------+
   | application programming (EGC, SPG, PP, SPRC, IOC, etc.) |
   +---------------------------------------------------------+

           +----------------------------------+
           | system programming (PC, SO, CPL) |
           +----------------------------------+
                                                     user space
   ----------------------------------------------------------=-
                                                   kernel space
             +--------------------------+
             | kernel programming (SO2) |
             +--------------------------+

   ----------------------------------------------------------=-

           +----------------------------------+
           |   hardware (PM, CN1, CN2, PL )   |
           +----------------------------------+

Resources
=========

* wiki: http://ocw.cs.pub.ro/courses/so2
* NeedToKnow: http://ocw.cs.pub.ro/courses/so2/need-to-know
* Linux Kernel Labs: https://linux-kernel-labs.github.io/
* mailing list: so2@cursuri.cs.pub.ro
* Facebook
* vmchecker
* catalog Google, calendar Google
* LXR
* cs.curs.pub.ro - rol de portal
* karma awards

Community
=========

* contribuții via https://github.com/linux-kernel-labs/linux (PR sau
  issues)
* corecții, ajustări, precizări, informații utile
* listă de discuții
* răspundeți la întrebările colegilor voștri
* propuneți subiecte de discuție care au legătură cu disciplina
* Facebook
* sugestii, propuneri, feedback
* Primiți puncte de karma

Grading
=======

* 2 puncte activitate la laborator
* 3 puncte „examen”, notare pe parcurs
* 10 puncte teme de casă
* Punctajul > 5 puncte e corelat direct proportional cu nota de la examen (la fel ca la SO)
* Tema 0 - 0,5 puncte
* Temele 1, 2, 3 - câte 1,5 puncte fiecare
* Activități “extra”
* Ixia challenge - 2 puncte
* Kernel (filesystem) hackaton - 2 puncte
* SO2 transport protocol - 1 punct
* Condiţii de promovare: nota finală 4.5, nota minimă examen 3

Obiectivele cursului
====================

* Prezentarea structurii interne a unui sistem de operare
* Target: sisteme de operare de uz general
* Structura și componentele unui kernel monolitic
* Procese, FS, Networking
* Memory management
* Exemplificare pe Linux

Obiectivele laboratorului/temelor
=================================
* Însușirea cunoștințelor necesare implementării de device drivere
* Înțelegerea în profunzime a cunoștințelor prin rezolvarea de exerciții

Cursuri necesare
================

* Programare: C
* SD: tabele de dispersie, arbori echilibrați
* IOCLA: lucrul cu registre și instrucțiuni de bază (adunări, comparaţii, salturi)
* CN: TLB/CAM, memorie, procesor, I/O
* PC, RL: ethernet, IP, sockeți
* SO: procese, fișiere, thread-uri, memorie virtuală

Despre curs
===========

* 12 cursuri
* interactiv
* participaţi la discuţii
* întrebaţi atunci când nu aţi înţeles
* destul de “dens”, se recomandă călduros parcurgerea suportului bibliografic înainte şi după curs
* 1h:30 prezentare + 30min test si discutii pe marginea testului

Despre curs (2)
===============

.. hlist::
   :columns: 2
   
   * Introducere
   * Procese
   * Scheduling
   * Apeluri de sistem
   * Traps
   * Spaţiul de adresă
   * Memorie virtuală
   * Memorie fizică
   * Kernel debugging
   * Block I/O
   * Sisteme de fişiere
   * SMP
   * Networking
   * Virtualizare


Despre laborator
================

* Kernel Modules and Device Drivers
* 15 min prezentare / 80 de minute lucru
* se punctează activitatea
* learn by doing

Despre teme
===========

* Tema 0
* Kprobe based tracer
* Driver pentru portul serial
* Software RAID
* Teme “extra”
* Filesystem driver - hackaton
* E100 driver - Ixia challenge
* Network transport protocol


Despre teme (2)
===============

* necesare: aprofundare API (laborator) și concepte (curs)
* teste publice
* suport de testare (vmchecker)
* relativ puţin cod de scris dar relativ dificile
* dificultatea constă în acomodarea cu noul mediu

Bibliografie curs
=================

* Linux Kernel Development, 3rd edition, Robert Love, Addison Wesley, 2010
* Understanding the Linux Kernel, 3rd edition, Daniel P. Bovet & Marco Cesati, O'Reilly 2005
* Linux Networking Architecture, Klaus Wehrle, Frank Pahlke, Hartmut Ritter, Daniel Muller, Marc Bechler, Prentice Hall 2004
* Understanding Linux Network Internals, Christian Benvenuti, O'Reilly 2005

Bibliografie laborator
======================

* Linux Device Drivers, 3nd edition, Alessandro Rubini & Jonathan Corbet, O'Reilly 2006
* Linux Kernel in a Nutshell, Greg Kroah-Hartman, O'Reilly 2005
