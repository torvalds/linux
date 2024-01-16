.. _it_linux_doc:

===================
Traduzione italiana
===================

.. raw:: latex

	\kerneldocCJKoff

:manutentore: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_disclaimer:

Avvertenze
==========

L'obiettivo di questa traduzione è di rendere più facile la lettura e
la comprensione per chi non capisce l'inglese o ha dubbi sulla sua
interpretazione, oppure semplicemente per chi preferisce leggere in lingua
italiana. Tuttavia, tenete ben presente che l'*unica* documentazione
ufficiale è quella in lingua inglese: :ref:`linux_doc`

La propagazione simultanea a tutte le traduzioni di una modifica in
:ref:`linux_doc` è altamente improbabile. I manutentori delle traduzioni -
e i contributori - seguono l'evolversi della documentazione ufficiale e
cercano di mantenere le rispettive traduzioni allineate nel limite del
possibile.  Per questo motivo non c'è garanzia che una traduzione sia
aggiornata all'ultima modifica.  Se quello che leggete in una traduzione
non corrisponde a quello che leggete nel codice, informate il manutentore
della traduzione e - se potete - verificate anche la documentazione in
inglese.

Una traduzione non è un *fork* della documentazione ufficiale, perciò gli
utenti non vi troveranno alcuna informazione diversa rispetto alla versione
ufficiale.  Ogni aggiunta, rimozione o modifica dei contenuti deve essere
fatta prima nei documenti in inglese. In seguito, e quando è possibile, la
stessa modifica dovrebbe essere applicata anche alle traduzioni.  I manutentori
delle traduzioni accettano contributi che interessano prettamente l'attività
di traduzione (per esempio, nuove traduzioni, aggiornamenti, correzioni).

Le traduzioni cercano di essere il più possibile accurate ma non è possibile
mappare direttamente una lingua in un'altra. Ogni lingua ha la sua grammatica
e una sua cultura alle spalle, quindi la traduzione di una frase in inglese
potrebbe essere modificata per adattarla all'italiano. Per questo motivo,
quando leggete questa traduzione, potreste trovare alcune differenze di forma
ma che trasmettono comunque il messaggio originale.  Nonostante la grande
diffusione di inglesismi nella lingua parlata, quando possibile, questi
verranno sostituiti dalle corrispettive parole italiane

Se avete bisogno d'aiuto per comunicare con la comunità Linux ma non vi sentite
a vostro agio nello scrivere in inglese, potete chiedere aiuto al manutentore
della traduzione.

La documentazione del kernel Linux
==================================

Questo è il livello principale della documentazione del kernel in
lingua italiana. La traduzione è incompleta, noterete degli avvisi
che vi segnaleranno la mancanza di una traduzione o di un gruppo di
traduzioni.

Più in generale, la documentazione, come il kernel stesso, sono in
costante sviluppo; particolarmente vero in quanto stiamo lavorando
alla riorganizzazione della documentazione in modo più coerente.
I miglioramenti alla documentazione sono sempre i benvenuti; per cui,
se vuoi aiutare, iscriviti alla lista di discussione linux-doc presso
vger.kernel.org.

Documentazione sulla licenza dei sorgenti
-----------------------------------------

I seguenti documenti descrivono la licenza usata nei sorgenti del kernel Linux
(GPLv2), come licenziare i singoli file; inoltre troverete i riferimenti al
testo integrale della licenza.

* :ref:`it_kernel_licensing`

Documentazione per gli utenti
-----------------------------

I seguenti manuali sono scritti per gli *utenti* del kernel - ovvero,
coloro che cercano di farlo funzionare in modo ottimale su un dato sistema

.. warning::

    TODO ancora da tradurre

Documentazione per gli sviluppatori di applicazioni
---------------------------------------------------

Il manuale delle API verso lo spazio utente è una collezione di documenti
che descrivono le interfacce del kernel viste dagli sviluppatori
di applicazioni.

.. warning::

    TODO ancora da tradurre


Introduzione allo sviluppo del kernel
-------------------------------------

Questi manuali contengono informazioni su come contribuire allo sviluppo
del kernel.
Attorno al kernel Linux gira una comunità molto grande con migliaia di
sviluppatori che contribuiscono ogni anno. Come in ogni grande comunità,
sapere come le cose vengono fatte renderà il processo di integrazione delle
vostre modifiche molto più semplice

.. toctree::
   :maxdepth: 2

   process/index
   doc-guide/index
   kernel-hacking/index

Documentazione della API del kernel
-----------------------------------

Questi manuali forniscono dettagli su come funzionano i sottosistemi del
kernel dal punto di vista degli sviluppatori del kernel. Molte delle
informazioni contenute in questi manuali sono prese direttamente dai
file sorgenti, informazioni aggiuntive vengono aggiunte solo se necessarie
(o almeno ci proviamo — probabilmente *non* tutto quello che è davvero
necessario).

.. toctree::
   :maxdepth: 2

   core-api/index

Documentazione specifica per architettura
-----------------------------------------

Questi manuali forniscono dettagli di programmazione per le diverse
implementazioni d'architettura.

.. warning::

    TODO ancora da tradurre
