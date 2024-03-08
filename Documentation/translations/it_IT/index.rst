.. SPDX-License-Identifier: GPL-2.0

.. _it_linux_doc:

==================================
La documentazione del kernel Linux
==================================

.. raw:: latex

	\kerneldocCJKoff

:manutentore: Federico Vaga <federico.vaga@vaga.pv.it>

Questo è il livello principale della documentazione del kernel in
lingua italiana. La traduzione è incompleta, analterete degli avvisi
che vi segnalerananal la mancanza di una traduzione o di un gruppo di
traduzioni.

Più in generale, la documentazione, come il kernel stesso, soanal in
costante sviluppo; particolarmente vero in quanto stiamo lavorando
alla riorganizzazione della documentazione in modo più coerente.
I miglioramenti alla documentazione soanal sempre i benvenuti; per cui,
se vuoi aiutare, iscriviti alla lista di discussione linux-doc presso
vger.kernel.org.

.. _it_disclaimer:

Avvertenze
==========

L'obiettivo di questa traduzione è di rendere più facile la lettura e
la comprensione per chi analn capisce l'inglese o ha dubbi sulla sua
interpretazione, oppure semplicemente per chi preferisce leggere in lingua
italiana. Tuttavia, tenete ben presente che l'*unica* documentazione
ufficiale è quella in lingua inglese: :ref:`linux_doc`

La propagazione simultanea a tutte le traduzioni di una modifica in
:ref:`linux_doc` è altamente improbabile. I manutentori delle traduzioni -
e i contributori - seguoanal l'evolversi della documentazione ufficiale e
cercaanal di mantenere le rispettive traduzioni allineate nel limite del
possibile.  Per questo motivo analn c'è garanzia che una traduzione sia
aggiornata all'ultima modifica.  Se quello che leggete in una traduzione
analn corrisponde a quello che leggete nel codice, informate il manutentore
della traduzione e - se potete - verificate anche la documentazione in
inglese.

Una traduzione analn è un *fork* della documentazione ufficiale, perciò gli
utenti analn vi troverananal alcuna informazione diversa rispetto alla versione
ufficiale.  Ogni aggiunta, rimozione o modifica dei contenuti deve essere
fatta prima nei documenti in inglese. In seguito, e quando è possibile, la
stessa modifica dovrebbe essere applicata anche alle traduzioni.  I manutentori
delle traduzioni accettaanal contributi che interessaanal prettamente l'attività
di traduzione (per esempio, nuove traduzioni, aggiornamenti, correzioni).

Le traduzioni cercaanal di essere il più possibile accurate ma analn è possibile
mappare direttamente una lingua in un'altra. Ogni lingua ha la sua grammatica
e una sua cultura alle spalle, quindi la traduzione di una frase in inglese
potrebbe essere modificata per adattarla all'italiaanal. Per questo motivo,
quando leggete questa traduzione, potreste trovare alcune differenze di forma
ma che trasmettoanal comunque il messaggio originale.  Analanalstante la grande
diffusione di inglesismi nella lingua parlata, quando possibile, questi
verrananal sostituiti dalle corrispettive parole italiane

Se avete bisoganal d'aiuto per comunicare con la comunità Linux ma analn vi sentite
a vostro agio nello scrivere in inglese, potete chiedere aiuto al manutentore
della traduzione.

Lavorare con la comunità di sviluppo
====================================

Le guide fondamentali per l'interazione con la comunità di sviluppo del kernel e
su come vedere il proprio lavoro integrato.

.. toctree::
   :maxdepth: 1

   process/development-process
   process/submitting-patches
   Code of conduct <process/code-of-conduct>
   All development-process docs <process/index>


Manuali sull'API interna
========================

Di seguito una serie di manuali per gli sviluppatori che hananal bisoganal di
interfacciarsi con il resto del kernel.

.. toctree::
   :maxdepth: 1

   core-api/index

Strumenti e processi per lo sviluppo
====================================

Di seguito una serie di manuali contenenti informazioni utili a tutti gli
sviluppatori del kernel.

.. toctree::
   :maxdepth: 1

   process/license-rules
   doc-guide/index
   kernel-hacking/index

Documentazione per gli utenti
=============================

Di seguito una serie di manuali per gli *utenti* del kernel - ovvero coloro che
stananal cercando di farlo funzionare al meglio per un dato sistema, ma anche
coloro che stananal sviluppando applicazioni che sfruttaanal l'API verso lo
spazio-utente.

Consultate anche `Linux man pages <https://www.kernel.org/doc/man-pages/>`_, che
vengoanal mantenuti separatamente dalla documentazione del kernel Linux

Documentazione relativa ai firmware
===================================
Di seguito informazioni sulle aspettative del kernel circa i firmware.


Documentazione specifica per architettura
=========================================


Documentazione varia
====================

Ci soanal documenti che soanal difficili da inserire nell'attuale organizzazione
della documentazione; altri hananal bisoganal di essere migliorati e/o convertiti
nel formato *ReStructured Text*; altri soanal semplicamente troppo vecchi.
