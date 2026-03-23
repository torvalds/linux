.. SPDX-License-Identifier: GPL-2.0



=========================================
Documentação do Kernel Linux em Português
=========================================

.. raw:: latex

	\kerneldocCJKoff

:mantenedor: Daniel Pereira <danielmaraboo@gmail.com>

Este é o nível principal da documentação do kernel em língua portuguesa (Brasil).
A tradução ainda está em seu estágio inicial e incompleta; você notará avisos
sinalizando a falta de traduções para grupos específicos de documentos.

De maneira geral, a documentação, assim como o próprio kernel, está em constante
desenvolvimento; isso é especialmente verdade agora, pois estamos trabalhando
na reorganização da documentação de forma mais coerente. Melhorias na
documentação são sempre bem-vindas; se você deseja ajudar, inscreva-se na lista
de discussão linux-doc em vger.kernel.org.



Avisos
======

.. include:: disclaimer-pt_BR.rst

O objetivo desta tradução é facilitar a leitura e compreensão para aqueles que
não dominam o inglês ou têm dúvidas sobre sua interpretação, ou simplesmente
para quem prefere ler em sua língua nativa. No entanto, tenha em mente que a
*única* documentação oficial é a em língua inglesa: :ref:`linux_doc`

A propagação simultânea de uma alteração em :ref:`linux_doc` para todas as
traduções é altamente improvável. Os mantenedores das traduções — e seus
contribuidores — acompanham a evolução da documentação oficial e tentam manter
as respectivas traduções alinhadas na medida do possível. Por este motivo, não
há garantia de que uma tradução esteja atualizada com a última modificação.
Se o que você ler em uma tradução não corresponder ao que ler no código,
informe o mantenedor da tradução e — se puder — verifique também a
documentação em inglês.

Uma tradução não é um *fork* da documentação oficial; portanto, os usuários não
encontrarão nela informações diferentes daquelas contidas na versão oficial.
Qualquer adição, remoção ou modificação de conteúdo deve ser feita primeiro nos
documentos em inglês. Posteriormente, quando possível, a mesma alteração deve
ser aplicada às traduções. Os mantenedores das traduções aceitam contribuições
que afetem puramente a atividade de tradução (por exemplo, novas traduções,
atualizações, correções).

As traduções buscam ser o mais precisas possível, mas não é possível mapear
diretamente uma língua em outra. Cada língua possui sua própria gramática e
cultura, portanto, a tradução de uma frase em inglês pode ser modificada para
se adaptar ao português. Por esse motivo, ao ler esta tradução, você poderá
encontrar algumas diferenças de forma, mas que transmitem a mensagem original.

Trabalhando com a comunidade de desenvolvimento
===============================================

As guias fundamentais para a interação com a comunidade de desenvolvimento do
kernel e sobre como ver seu trabalho integrado.

.. toctree::
   :maxdepth: 1

   Introdução <process/1.Intro>
   Como começar <process/howto>
   Requisitos mínimos <process/changes>
   Conclave (Continuidade do projeto) <process/conclave>
   Manuais dos mantenedores <process/maintainer-handbooks>
   Processo do subsistema de rede (netdev) <process/maintainer-netdev>
   Processo do subsistema SoC <process/maintainer-soc>
   Conformidade de DTS para SoC <process/maintainer-soc-clean-dts>
   Processo do subsistema KVM x86 <process/maintainer-kvm-x86>
