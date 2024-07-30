.. raw:: latex

	\renewcommand\thesection*
	\renewcommand\thesubsection*

.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/index.rst <process_index>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_process_index:

===============================================
Lavorare con la comunità di sviluppo del kernel
===============================================

Quindi volete diventare sviluppatori del kernel?  Benvenuti! C'è molto da
imparare sul lato tecnico del kernel, ma è anche importante capire come
funziona la nostra comunità.  Leggere questi documenti renderà più facile
l'accettazione delle vostre modifiche con il minimo sforzo.

Di seguito le guide che ogni sviluppatore dovrebbe leggere.

Introduzione al funzionamento dello sviluppo del kernel
-------------------------------------------------------

Innanzitutto, leggete questi documenti che vi aiuteranno ad entrare nella
comunità del kernel.

.. toctree::
   :maxdepth: 1

   howto
   development-process
   submitting-patches
   submit-checklist

Strumenti e guide tecniche per gli sviluppatori del kernel
----------------------------------------------------------

Quella che segue è una raccolta di documenti che uno sviluppatore del kernel
Linux dovrebbe conoscere.

.. toctree::
   :maxdepth: 1

   changes
   programming-language
   coding-style
   maintainer-pgp-guide
   email-clients
   applying-patches
   adding-syscalls
   volatile-considered-harmful
   botching-up-ioctls

Politiche e dichiarazioni degli sviluppatori
--------------------------------------------

Quelle che seguono rappresentano le regole che cerchiamo di seguire all'interno
della comunità del kernel (e oltre).

.. toctree::
   :maxdepth: 1

   code-of-conduct
   kernel-enforcement-statement
   kernel-driver-statement
   stable-api-nonsense
   stable-kernel-rules
   management-style

Gestire i bachi
---------------

I bachi sono parte della nostra vita; dunque è importante che vengano trattati
con riguardo. I documenti che seguono descrivono le nostre politiche riguardo al
trattamento di alcune classi particolari di bachi: le regressioni e i problemi
di sicurezza.

Informazioni per i manutentori
------------------------------

Come trovare le persone che accetteranno le vostre modifiche.

.. toctree::
   :maxdepth: 1

   maintainers

Altri documenti
---------------

Poi ci sono altre guide sulla comunità che sono di interesse per molti
degli sviluppatori:

.. toctree::
   :maxdepth: 1

   kernel-docs

Ed infine, qui ci sono alcune guide più tecniche che son state messe qua solo
perché non si è trovato un posto migliore.

.. toctree::
   :maxdepth: 1

   magic-number
   clang-format
   ../arch/riscv/patch-acceptance

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`
