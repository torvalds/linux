==============================================================
SO2 Lecture 01 - Course overview and Linux kernel introduction
==============================================================

`View slides <lec1-intro-slides.html>`_

.. slideconf::
   :autoslides: False
   :theme: single-level

.. slide:: SO2 Lecture 01 - Course overview and Linux kernel introduction
   :inline-contents: False
   :level: 1


Echipa
======

.. slide:: Echipa
   :inline-contents: True
   :level: 2

   * Daniel Băluță (Daniel), Răzvan Deaconescu (Răzvan, RD), Claudiu
     Ghioc (Claudiu), Valentin Ghiță (Vali), Sergiu Weisz (Sergiu),
     Octavian Purdilă (Tavi)

   * Alexandru Militaru (Alex), Teodora Șerbănescu (Teo), Ștefan
     Teodorescu (Ștefan, Fane), Mihai Popescu (Mihai, Mișu),
     Constantin Răducanu, Daniel Dinca, Laurențiu Ștefan

   * Mult succes în noul semestru!

Poziționare curs
================

.. slide:: Poziționare curs
   :inline-contents: True
   :level: 2

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

Resurse
=======

.. slide:: Resurse
   :inline-contents: True
   :level: 2

   * Linux Kernel Labs: https://linux-kernel-labs.github.io/
   * mailing list: so2@cursuri.cs.pub.ro
   * Facebook
   * vmchecker
   * catalog Google, calendar Google
   * LXR: https://elixir.bootlin.com/linux/v5.10.14/source
   * cs.curs.pub.ro - rol de portal
   * karma awards

Comunitate
==========

.. slide:: Comunitate
   :inline-contents: True
   :level: 2

   * tutorial contribuții: https://linux-kernel-labs.github.io/refs/heads/master/info/contributing.html
   * corecții, ajustări, precizări, informații utile
   * listă de discuții
   * răspundeți la întrebările colegilor voștri
   * propuneți subiecte de discuție care au legătură cu disciplina
   * Facebook
   * sugestii, propuneri, feedback
   * Primiți puncte de karma

Notare
=======

.. slide:: Notare
   :inline-contents: True
   :level: 2

   * 2 puncte activitate la laborator
   * 3 puncte „examen”, notare pe parcurs
   * 5 puncte teme de casă
   * Activități "extra"
   * Punctajul din teme de casă + activitați extra ce depăsește 5
     puncte e corelat direct proportional cu nota de la examen
   * Tema 0 - 0,5 puncte
   * Temele 1, 2, 3 - câte 1,5 puncte fiecare
   * Condiţii de promovare: nota finală 4.5, nota minimă examen 3

Obiectivele cursului
====================

.. slide:: Obiectivele cursului
   :inline-contents: True
   :level: 2

   * Prezentarea structurii interne a unui sistem de operare
   * Target: sisteme de operare de uz general
   * Structura și componentele unui kernel monolitic
   * Procese, FS, Networking
   * Memory management
   * Exemplificare pe Linux

Obiectivele laboratorului si a temelor
======================================

.. slide:: Obiectivele laboratorului si a temelor
   :inline-contents: True
   :level: 2

   * Însușirea cunoștințelor necesare implementării de device drivere

   * Înțelegerea în profunzime a cunoștințelor prin rezolvarea de
     exerciții

Cursuri necesare
================

.. slide:: Cursuri necesare
   :inline-contents: True
   :level: 2

   * Programare: C
   * SD: tabele de dispersie, arbori echilibrați
   * IOCLA: lucrul cu registre și instrucțiuni de bază (adunări, comparaţii, salturi)
   * CN: TLB/CAM, memorie, procesor, I/O
   * PC, RL: ethernet, IP, sockeți
   * SO: procese, fișiere, thread-uri, memorie virtuală

Despre curs
===========

.. slide:: Despre curs
   :inline-contents: True
   :level: 2

   * 12 cursuri
   * interactiv
   * participaţi la discuţii
   * întrebaţi atunci când nu aţi înţeles
   * destul de “dens”, se recomandă călduros parcurgerea suportului bibliografic înainte şi după curs
   * 1h:20 prezentare + 20min teste si discutii pe marginea testului

Lista cursuri
=============

.. slide:: Lista cursuri
   :inline-contents: True
   :level: 2

   .. hlist::
      :columns: 2

      * Introducere
      * Apeluri de sistem
      * Procese
      * Întreruperi
      * Sincronizare
      * Adresarea memoriei
      * Gestiunea memoriei
      * Gestiunea fișierelor
      * Kernel debugging
      * Gestiunea rețelei
      * Virtualizare
      * Kernel profiling


Despre laborator
================

.. slide:: Despre laborator
   :inline-contents: True
   :level: 2

   * Kernel Modules and Device Drivers
   * 15 min prezentare / 80 de minute lucru
   * se punctează activitatea
   * learn by doing

Despre teme
===========

.. slide:: Despre teme
   :inline-contents: True
   :level: 2

   * necesare: aprofundare API (laborator) și concepte (curs)
   * teste publice
   * suport de testare (vmchecker)
   * relativ puţin cod de scris dar relativ dificile
   * dificultatea constă în acomodarea cu noul mediu

Lista teme
==========

.. slide:: Lista teme
   :inline-contents: True
   :level: 2

   * Tema 0 - Kernel API
   * Kprobe based tracer
   * Driver pentru portul serial
   * Software RAID
   * SO2 Transport Protocol


Bibliografie curs
=================

.. slide:: Bibliografie curs
   :inline-contents: True
   :level: 2

   * Linux Kernel Development, 3rd edition, Robert Love, Addison
     Wesley, 2010

   * Understanding the Linux Kernel, 3rd edition, Daniel P. Bovet &
     Marco Cesati, O'Reilly 2005

   * Linux Networking Architecture, Klaus Wehrle, Frank Pahlke,
     Hartmut Ritter, Daniel Muller, Marc Bechler, Prentice Hall 2004

   * Understanding Linux Network Internals, Christian Benvenuti, O'Reilly 2005

Bibliografie laborator
======================

.. slide:: Bibliografie laborator
   :inline-contents: True
   :level: 2

   * Linux Device Drivers, 3nd edition, Alessandro Rubini & Jonathan
     Corbet, O'Reilly 2006

   * Linux Kernel in a Nutshell, Greg Kroah-Hartman, O'Reilly 2005


.. include:: ../lectures/intro.rst
   :start-line: 6
