.. SPDX-License-Identifier: GPL-2.0

.. raw:: latex

	\renewcommand\thesection*
	\renewcommand\thesubsection*

===============================================
Trabalhando com a comunidade de desenvolvimento
===============================================

Então você quer ser um desenvolvedor do kernel Linux? Bem-vindo! Embora haja
muito a aprender sobre o kernel em um sentido técnico, também é importante
aprender como nossa comunidade funciona. A leitura desses documentos tornará
muito mais fácil para você ter suas alterações integradas com um mínimo de
problemas.

Uma introdução sobre como funciona o desenvolvimento do kernel
--------------------------------------------------------------

Leia estes documentos primeiro: entender este material facilitará
sua entrada na comunidade do kernel.

.. toctree::
   :maxdepth: 1

   Como começar <howto>
   Guia do Processo de Desenvolvimento <development-process>
   Lista de verificação para submissão de patches do kernel Linux <submit-checklist>

Ferramentas e guias técnicos para desenvolvedores do kernel
-----------------------------------------------------------

Esta é uma coleção de material com o qual os desenvolvedores do kernel
devem estar familiarizados.

.. toctree::
   :maxdepth: 1

   Requisitos mínimos <changes>
   Informações sobre clientes de email para Linux <email-clients>
   Como aplicar patches <applying-patches>
   Backporting e resolução de conflitos <backporting>
   Adicionando uma nova chamada de Sistema <adding-syscalls>
   Como não Deixar as ioctls malfeitas <botching-up-ioctls>

Guias de políticas e declarações de desenvolvedores
---------------------------------------------------

Estas são as regras pelas quais tentamos viver na comunidade do kernel
(e além).

.. toctree::
   :maxdepth: 1

   Regras de licenciamento <license-rules>
   Código de Conduta de Compromisso do Colaborador <code-of-conduct>
   Interpretação do Código de Conduta do Kernel Linux <code-of-conduct-interpretation>
   Modelos de Maturidade para Contribuição no Kernel Linux <contribution-maturity-model.rst>
   Declaração sobre Drivers do Kernel <kernel-driver-statement>
   Estilo de gerenciamento do kernel Linux <management-style>
   Conclave (Continuidade do projeto) <conclave>

Lidando com bugs
----------------

Bugs são uma realidade; é importante lidarmos com eles de forma correta.
Os documentos abaixo detalham políticas e conselhos em relação ao
gerenciamento de bugs e vulnerabilidades.

.. toctree::
   :maxdepth: 1

   Falhas de segurança <security-bugs>
   CVEs <cve>

Informações para mantenedores
-----------------------------

Como encontrar as pessoas que aceitarão seus patches e manuais úteis para os
mantenedores de subsistemas.

.. toctree::
   :maxdepth: 1

   Manuais dos mantenedores <maintainer-handbooks>
   Processo do subsistema de rede (netdev) <maintainer-netdev>
   Processo do subsistema SoC <maintainer-soc>
   Conformidade de DTS para SoC <maintainer-soc-clean-dts>
   Processo do subsistema KVM x86 <maintainer-kvm-x86>

Outros materiais
----------------

Aqui estão alguns outros guias para a comunidade que são de interesse para
a maioria dos desenvolvedores:

.. toctree::
   :maxdepth: 1

   Index de documentos do Kernel <kernel-docs>
   Interfaces, recursos de linguagem, atributos e convenções obsoletos <deprecated>
